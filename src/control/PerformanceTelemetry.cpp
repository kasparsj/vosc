#include "PerformanceTelemetry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace control {

bool PerformanceContext::operator==(const PerformanceContext& other) const {
    return revision == other.revision && externalEpoch == other.externalEpoch &&
           width == other.width && height == other.height && feedbackGeneration == other.feedbackGeneration;
}

void PerformanceSampleWindow::add(uint64_t frame, double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0)
        throw std::invalid_argument("performance sample must be finite and nonnegative");
    auto position = std::lower_bound(values.begin(), values.end(), frame,
        [](const Sample& sample, uint64_t candidate) { return sample.frame < candidate; });
    if (position != values.end() && position->frame == frame) position->milliseconds = milliseconds;
    else values.insert(position, {frame, milliseconds});
    advanceTo(frame);
}

void PerformanceSampleWindow::advanceTo(uint64_t newestFrame) {
    if (!hasNewestFrame || newestFrame > this->newestFrame) {
        this->newestFrame = newestFrame;
        hasNewestFrame = true;
    }
    const uint64_t oldest = this->newestFrame >= Capacity - 1
        ? this->newestFrame - (Capacity - 1) : 0;
    while (!values.empty() && values.front().frame < oldest) values.pop_front();
}

void PerformanceSampleWindow::clear() {
    values.clear();
    hasNewestFrame = false;
    newestFrame = 0;
}

PerformanceStats PerformanceSampleWindow::stats() const {
    PerformanceStats result;
    result.samples = values.size();
    if (values.empty()) return result;
    std::vector<double> ordered;
    ordered.reserve(values.size());
    for (const auto& sample : values) ordered.push_back(sample.milliseconds);
    std::sort(ordered.begin(), ordered.end());
    auto nearestRank = [&ordered](double percentile) {
        size_t rank = static_cast<size_t>(std::ceil(percentile * ordered.size()));
        return ordered[std::max<size_t>(1, rank) - 1];
    };
    result.mean = std::accumulate(ordered.begin(), ordered.end(), 0.0) / ordered.size();
    result.p50 = nearestRank(0.50);
    result.p95 = nearestRank(0.95);
    result.maximum = ordered.back();
    return result;
}

PerformanceTelemetry::PerformanceTelemetry(std::unique_ptr<GpuQueryBackend> backend)
    : backend(std::move(backend)) {
    if (!this->backend) throw std::invalid_argument("GPU query backend is required");
}

PerformanceTelemetry::~PerformanceTelemetry() { shutdown(); }

void PerformanceTelemetry::setup() {
    if (initialized) return;
    backend->initialize();
    initialized = true;
}

void PerformanceTelemetry::shutdown() {
    if (!initialized) return;
    if (currentQuery) {
        backend->end();
        currentQuery.reset();
    }
    backend->shutdown();
    initialized = false;
    pending = {};
}

void PerformanceTelemetry::resetWindow(const PerformanceContext& next) {
    context = next;
    hasContext = true;
    ++generation;
    previousStart.reset();
    frameIntervals.clear();
    cpuSubmissions.clear();
    gpuTimes.clear();
    gpuSamples.clear();
    gpuLifetimeMaximum.reset();
    gpuLastFrame.reset();
    droppedSamples = 0;
    attempts = 0;
}

void PerformanceTelemetry::refreshGpuResults() {
    if (!initialized) return;
    if (!backend->supported()) {
        pending = {};
        gpuTimes.clear();
        gpuSamples.clear();
        gpuLastFrame.reset();
        return;
    }
    for (size_t slot = 0; slot < pending.size(); ++slot) {
        if (!pending[slot].active || !backend->available(slot)) continue;
        const auto nanoseconds = backend->resultNanoseconds(slot);
        const auto sample = pending[slot];
        pending[slot].active = false;
        const double milliseconds = static_cast<double>(nanoseconds) / 1000000.0;
        if (sample.measurement && gpuObserver) gpuObserver(sample.measurement, sample.frame, milliseconds);
        if (sample.generation != generation || sample.context != context) continue;
        if (!gpuLifetimeMaximum || milliseconds > *gpuLifetimeMaximum)
            gpuLifetimeMaximum = milliseconds;
        if (newestFrame >= PerformanceSampleWindow::Capacity - 1 &&
            sample.frame < newestFrame - (PerformanceSampleWindow::Capacity - 1)) continue;
        gpuTimes.add(sample.frame, milliseconds);
        auto found = std::lower_bound(gpuSamples.begin(), gpuSamples.end(), sample.frame,
            [](const auto& item, uint64_t frame) { return item.first < frame; });
        if (found != gpuSamples.end() && found->first == sample.frame) found->second = milliseconds;
        else gpuSamples.insert(found, {sample.frame, milliseconds});
        if (!gpuLastFrame || sample.frame > *gpuLastFrame) gpuLastFrame = sample.frame;
    }
}

bool PerformanceTelemetry::beginFrame(const PerformanceContext& next, uint64_t frame,
                                      double monotonicSeconds, uint64_t measurement) {
    if (!initialized || !std::isfinite(monotonicSeconds)) return false;
    refreshGpuResults();
    if (!hasContext || next != context) resetWindow(next);
    newestFrame = frame;
    ++attempts;
    frameIntervals.advanceTo(frame);
    cpuSubmissions.advanceTo(frame);
    gpuTimes.advanceTo(frame);
    const uint64_t oldest = frame >= PerformanceSampleWindow::Capacity - 1
        ? frame - (PerformanceSampleWindow::Capacity - 1) : 0;
    while (!gpuSamples.empty() && gpuSamples.front().first < oldest) gpuSamples.pop_front();
    if (gpuTimes.empty()) gpuLastFrame.reset();
    if (previousStart && monotonicSeconds >= *previousStart)
        frameIntervals.add(frame, (monotonicSeconds - *previousStart) * 1000.0);
    previousStart = monotonicSeconds;
    cpuStart = monotonicSeconds;
    currentQuery.reset();
    if (!backend->supported()) return false;
    for (size_t slot = 0; slot < pending.size(); ++slot) {
        if (!pending[slot].active) {
            if (backend->begin(slot)) {
                pending[slot] = Pending{true, generation, context, frame, measurement};
                currentQuery = slot;
                return true;
            } else {
                ++droppedSamples;
                if (!backend->supported()) {
                pending = {};
                gpuTimes.clear();
                gpuLastFrame.reset();
                }
            }
            return false;
        }
    }
    ++droppedSamples;
    return false;
}

void PerformanceTelemetry::endComposedDraw(double monotonicSeconds) {
    if (!initialized || !cpuStart || !std::isfinite(monotonicSeconds)) return;
    if (monotonicSeconds >= *cpuStart)
        cpuSubmissions.add(newestFrame, (monotonicSeconds - *cpuStart) * 1000.0);
    cpuStart.reset();
    if (currentQuery) {
        backend->end();
        currentQuery.reset();
    }
}

size_t PerformanceTelemetry::pendingCount() const {
    return static_cast<size_t>(std::count_if(pending.begin(), pending.end(),
                                            [](const Pending& item) { return item.active; }));
}

PerformanceSnapshot PerformanceTelemetry::snapshot(const PerformanceContext& current) const {
    PerformanceSnapshot result;
    result.hasContext = hasContext;
    result.context = context;
    result.matchesCurrentContext = hasContext && context == current;
    result.frameInterval = frameIntervals.stats();
    result.cpuSubmit = cpuSubmissions.stats();
    result.gpu = gpuTimes.stats();
    result.gpuSupported = initialized && backend->supported();
    result.gpuState = !result.gpuSupported ? "unsupported" :
                      result.gpu.samples == 0 ? "warming_up" : "available";
    if (!result.gpuSupported) result.gpuReason = backend->reason();
    result.gpuLastFrame = gpuLastFrame;
    result.pendingQueries = pendingCount();
    result.droppedSamples = droppedSamples;
    result.attempts = attempts;
    result.lastAttemptFrame = newestFrame;
    result.rawSamples = gpuSamples;
    return result;
}

FilteredGpuSnapshot PerformanceTelemetry::filteredGpuSnapshot(
    const PerformanceContext& expected, uint64_t firstFrame, uint64_t warmupFrames) const {
    FilteredGpuSnapshot result;
    result.supported = initialized && backend->supported();
    result.matchesContext = hasContext && context == expected;
    if (!result.supported || !result.matchesContext) return result;
    PerformanceSampleWindow filtered;
    const uint64_t firstMeasured = firstFrame > UINT64_MAX - warmupFrames
        ? UINT64_MAX : firstFrame + warmupFrames;
    for (const auto& sample : gpuSamples) {
        if (sample.first >= firstMeasured) filtered.add(sample.first, sample.second);
    }
    result.samples = filtered.stats();
    result.lifetimeMaximum = gpuLifetimeMaximum;
    result.lastFrame = gpuLastFrame;
    return result;
}

void FeedbackGpuMeasurement::reset() {
    *this = {};
}

void FeedbackGpuMeasurement::attempt(uint64_t frame, double now, bool update, bool draw) {
    ++attempts;
    if (!update) ++updateDrops;
    if (!draw) ++drawDrops;
    std::optional<double> interval;
    if (previousStart) interval = (now - *previousStart) * 1000.0;
    previousStart = now;
    recent.push_back({frame, interval, update, draw});
    if (update && draw) pending.try_emplace(frame);
    oldest = frame >= PerformanceSampleWindow::Capacity - 1 ? frame - (PerformanceSampleWindow::Capacity - 1) : 0;
    while (!recent.empty() && recent.front().frame < oldest) recent.pop_front();
    results.erase(results.begin(), results.lower_bound(oldest));
    // Each entry holds at least one outstanding query slot. Keep expired
    // entries until completion so late totals still reach the hard maximum.
    if (!pending.empty() && pending.begin()->first < oldest) agedPending = true;
}

void FeedbackGpuMeasurement::complete(uint64_t frame, bool draw, double milliseconds) {
    const auto found = pending.find(frame);
    if (found == pending.end()) return;
    auto& halves = found->second;
    (draw ? halves.draw : halves.update) = milliseconds;
    if (!halves.update || !halves.draw) return;
    const auto total = *halves.update + *halves.draw;
    if (!maximum || total > *maximum) maximum = total;
    if (frame >= oldest) results.emplace(frame, Pair{*halves.update, *halves.draw});
    pending.erase(found);
}

FilteredGpuSnapshot FeedbackGpuMeasurement::admission(uint64_t firstMeasured, bool supported, bool matches) const {
    FilteredGpuSnapshot result;
    result.supported = supported;
    result.matchesContext = matches;
    result.lifetimeMaximum = maximum;
    result.agedPending = agedPending;
    PerformanceSampleWindow filtered;
    for (auto pair = results.lower_bound(firstMeasured); pair != results.end(); ++pair) {
        filtered.add(pair->first, pair->second.update + pair->second.draw);
        result.lastFrame = pair->first;
    }
    result.samples = filtered.stats();
    if (result.lastFrame) {
        const auto eligible = *result.lastFrame - std::max(firstMeasured, oldest) + 1;
        result.coverage = double(result.samples.samples) / eligible;
    } else result.coverage = 0;
    return result;
}

} // namespace control
