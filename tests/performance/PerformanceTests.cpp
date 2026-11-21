#include "control/OscPort.hpp"
#include "control/PerformanceTelemetry.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace control;

namespace {

class FakeBackend final : public GpuQueryBackend {
public:
    bool initialize() override { initialized = true; return support; }
    void shutdown() override { initialized = false; }
    bool supported() const override { return initialized && support; }
    std::string reason() const override { return support ? std::string() : "timer unavailable"; }
    bool begin(size_t slot) override {
        ++begins;
        if (!beginSucceeds) {
            if (disableOnBeginFailure) support = false;
            return false;
        }
        current = slot;
        return true;
    }
    void end() override { current = Capacity; ++ends; }
    bool available(size_t slot) override { ++availabilityChecks; return ready[slot]; }
    uint64_t resultNanoseconds(size_t slot) override {
        assert(ready[slot]);
        ready[slot] = false;
        ++resultReads;
        return results[slot];
    }

    bool support = true;
    bool beginSucceeds = true;
    bool disableOnBeginFailure = false;
    bool initialized = false;
    size_t current = Capacity;
    int begins = 0;
    int ends = 0;
    int availabilityChecks = 0;
    int resultReads = 0;
    std::array<bool, Capacity> ready{};
    std::array<uint64_t, Capacity> results{};
};

void expectInvalid(const char* value) {
    try {
        (void)parseOscPort(value);
        assert(false && "expected invalid port");
    } catch (const std::invalid_argument&) {}
}

void testPortParsing() {
    assert(parseOscPort(nullptr) == 33333);
    assert(parseOscPort("0") == 0);
    assert(parseOscPort("65535") == 65535);
    expectInvalid("-1");
    expectInvalid(" 1");
    expectInvalid("");
    expectInvalid("12x");
    expectInvalid("65536");
    expectInvalid("999999999999999999999999");
}

void testStatisticsAndFrameRetention() {
    PerformanceSampleWindow window;
    auto empty = window.stats();
    assert(empty.samples == 0 && !empty.mean && !empty.p95);
    for (uint64_t frame = 1; frame <= 120; ++frame) window.add(frame, static_cast<double>(frame));
    auto full = window.stats();
    assert(full.samples == 120);
    assert(*full.mean == 60.5 && *full.p50 == 60.0 && *full.p95 == 114.0 && *full.maximum == 120.0);
    window.advanceTo(121);
    auto aged = window.stats();
    assert(aged.samples == 119 && *aged.p50 == 61.0);
    window.add(120, 999.0); // Replacement and out-of-order insertion remain frame-bounded.
    window.add(2, 2.0);
    assert(window.stats().samples == 119);
    try { window.add(122, -1); assert(false); } catch (const std::invalid_argument&) {}
}

void testTelemetryLifecycleAndNoWait() {
    auto owned = std::make_unique<FakeBackend>();
    auto* fake = owned.get();
    PerformanceTelemetry telemetry(std::move(owned));
    telemetry.setup();
    PerformanceContext first{1, 2, 640, 480};
    telemetry.beginFrame(first, 1, 1.000);
    telemetry.endComposedDraw(1.004);
    assert(fake->begins == 1 && fake->ends == 1 && fake->resultReads == 0);
    auto warming = telemetry.snapshot(first);
    assert(warming.gpuSupported && warming.gpuState == "warming_up" && warming.pendingQueries == 1);
    fake->ready[0] = true;
    fake->results[0] = 2'500'000;
    telemetry.beginFrame(first, 2, 1.016);
    telemetry.endComposedDraw(1.021);
    auto available = telemetry.snapshot(first);
    assert(available.frameInterval.samples == 1 && available.cpuSubmit.samples == 2);
    assert(available.gpu.samples == 1 && *available.gpu.p50 == 2.5 && available.gpuLastFrame == 1);

    PerformanceContext changed{2, 2, 640, 480};
    telemetry.beginFrame(changed, 3, 1.032);
    telemetry.endComposedDraw(1.034);
    auto reset = telemetry.snapshot(changed);
    assert(reset.frameInterval.samples == 0 && reset.cpuSubmit.samples == 1 && reset.gpu.samples == 0);
    assert(!telemetry.snapshot(first).matchesCurrentContext);
    PerformanceContext epochChanged{2, 3, 640, 480};
    telemetry.beginFrame(epochChanged, 4, 1.048);
    telemetry.endComposedDraw(1.050);
    assert(telemetry.snapshot(epochChanged).frameInterval.samples == 0);
    PerformanceContext resized{2, 3, 800, 600};
    telemetry.beginFrame(resized, 5, 1.064);
    telemetry.endComposedDraw(1.066);
    assert(telemetry.snapshot(resized).frameInterval.samples == 0);
    telemetry.shutdown();
    assert(!fake->initialized);
}

void testFullPoolDropsAndLateSamplesAreDiscarded() {
    auto owned = std::make_unique<FakeBackend>();
    auto* fake = owned.get();
    PerformanceTelemetry telemetry(std::move(owned));
    telemetry.setup();
    PerformanceContext context{1, 0, 100, 100};
    for (uint64_t frame = 1; frame <= 8; ++frame) {
        telemetry.beginFrame(context, frame, frame);
        telemetry.endComposedDraw(frame + 0.1);
    }
    telemetry.beginFrame(context, 9, 9.0);
    telemetry.endComposedDraw(9.1);
    auto full = telemetry.snapshot(context);
    assert(full.pendingQueries == 8 && full.droppedSamples == 1 && fake->resultReads == 0);

    PerformanceContext changed{2, 0, 100, 100};
    fake->ready[0] = true;
    fake->results[0] = 1'000'000;
    telemetry.beginFrame(changed, 10, 10.0);
    telemetry.endComposedDraw(10.1);
    assert(telemetry.snapshot(changed).gpu.samples == 0);

    // A delayed result from the current generation that originated outside the 120-frame window is omitted.
    fake->ready[1] = true; // frees an old-generation slot during the next begin
    telemetry.beginFrame(changed, 11, 11.0);
    telemetry.endComposedDraw(11.1);
    const size_t newSlot = 1;
    telemetry.beginFrame(changed, 131, 131.0);
    telemetry.endComposedDraw(131.1);
    fake->ready[newSlot] = true;
    fake->results[newSlot] = 9'000'000;
    telemetry.beginFrame(changed, 132, 132.0);
    telemetry.endComposedDraw(132.1);
    assert(telemetry.snapshot(changed).gpu.samples == 0);
}

void testUnsupportedIsTruthful() {
    auto owned = std::make_unique<FakeBackend>();
    owned->support = false;
    PerformanceTelemetry telemetry(std::move(owned));
    telemetry.setup();
    PerformanceContext context{};
    telemetry.beginFrame(context, 1, 1.0);
    telemetry.endComposedDraw(1.1);
    auto result = telemetry.snapshot(context);
    assert(!result.gpuSupported && result.gpuState == "unsupported");
    assert(result.gpuReason == "timer unavailable" && result.pendingQueries == 0);
}

void testLastFrameExpiresAndLongUptimeKeepsPrecision() {
    auto owned = std::make_unique<FakeBackend>();
    auto* fake = owned.get();
    PerformanceTelemetry telemetry(std::move(owned));
    telemetry.setup();
    PerformanceContext context{1, 0, 100, 100};
    telemetry.beginFrame(context, 1, 65536.000);
    telemetry.endComposedDraw(65536.001);
    fake->ready[0] = true;
    fake->results[0] = 1'000'000;
    telemetry.beginFrame(context, 2, 65536.016);
    telemetry.endComposedDraw(65536.017);
    auto precise = telemetry.snapshot(context);
    assert(std::abs(*precise.frameInterval.p50 - 16.0) < 0.000001);
    assert(std::abs(*precise.cpuSubmit.p50 - 1.0) < 0.000001);
    assert(precise.gpu.samples == 1 && precise.gpuLastFrame == 1);
    for (uint64_t frame = 3; frame <= 121; ++frame) {
        const double start = 65536.0 + static_cast<double>(frame) * 0.016;
        telemetry.beginFrame(context, frame, start);
        telemetry.endComposedDraw(start + 0.001);
    }
    auto expired = telemetry.snapshot(context);
    assert(expired.gpu.samples == 0 && !expired.gpuLastFrame);
}

void testRuntimeBackendFailureClearsPendingState() {
    auto owned = std::make_unique<FakeBackend>();
    auto* fake = owned.get();
    PerformanceTelemetry telemetry(std::move(owned));
    telemetry.setup();
    PerformanceContext context{1, 0, 100, 100};
    telemetry.beginFrame(context, 1, 1.0);
    telemetry.endComposedDraw(1.1);
    assert(telemetry.snapshot(context).pendingQueries == 1);
    fake->beginSucceeds = false;
    fake->disableOnBeginFailure = true;
    telemetry.beginFrame(context, 2, 2.0);
    telemetry.endComposedDraw(2.1);
    auto failed = telemetry.snapshot(context);
    assert(!failed.gpuSupported && failed.gpuState == "unsupported");
    assert(failed.pendingQueries == 0 && failed.gpu.samples == 0 && !failed.gpuLastFrame);
}

void testFilteredGpuSnapshotLatchesLateHardMaximumAndRefreshesWithoutFrameStart() {
    auto owned = std::make_unique<FakeBackend>();
    auto* fake = owned.get();
    PerformanceTelemetry telemetry(std::move(owned));
    telemetry.setup();
    PerformanceContext context{7, 2, 640, 480};
    telemetry.beginFrame(context, 1, 1.0);
    telemetry.endComposedDraw(1.1);
    telemetry.beginFrame(context, 121, 2.0);
    telemetry.endComposedDraw(2.1);
    fake->ready[0] = true;
    fake->results[0] = 20'000'000;
    telemetry.refreshGpuResults();
    auto filtered = telemetry.filteredGpuSnapshot(context, 1, 8);
    assert(filtered.supported && filtered.matchesContext);
    assert(filtered.samples.samples == 0);
    assert(filtered.lifetimeMaximum && *filtered.lifetimeMaximum == 20.0);
    telemetry.shutdown();
}

void testSeparateStreamsKeepRawOriginAndGeneration() {
    auto updateOwned = std::make_unique<FakeBackend>();
    auto drawOwned = std::make_unique<FakeBackend>();
    auto* update = updateOwned.get();
    auto* draw = drawOwned.get();
    PerformanceTelemetry updates(std::move(updateOwned)), draws(std::move(drawOwned));
    PerformanceContext context{1, 2, 800, 600, 9};
    // Disabled telemetry allocates/starts no queries.
    updates.beginFrame(context, 1, 1.0);
    assert(update->begins == 0 && !update->initialized);
    updates.setup(); draws.setup();
    for (uint64_t frame = 1; frame <= 2; ++frame) {
        updates.beginFrame(context, frame, frame);
        updates.endComposedDraw(frame + .01);
        draws.beginFrame(context, frame, frame + .02);
        draws.endComposedDraw(frame + .03);
    }
    // Update frame 2 returns ahead of frame 1. Draw frame 1 returns first.
    update->ready[1] = true; update->results[1] = 2'000'000;
    draw->ready[0] = true; draw->results[0] = 5'000'000;
    updates.refreshGpuResults(); draws.refreshGpuResults();
    auto u = updates.snapshot(context), d = draws.snapshot(context);
    assert(u.rawSamples.size() == 1 && u.rawSamples[0].first == 2);
    assert(d.rawSamples.size() == 1 && d.rawSamples[0].first == 1);
    assert(u.attempts == 2 && d.attempts == 2 && u.lastAttemptFrame == 2);
    update->ready[0] = true; update->results[0] = 1'000'000;
    draw->ready[1] = true; draw->results[1] = 8'000'000;
    updates.refreshGpuResults(); draws.refreshGpuResults();
    u = updates.snapshot(context); d = draws.snapshot(context);
    assert(u.rawSamples[0].first == 1 && u.rawSamples[1].first == 2);
    assert(u.rawSamples[0].second + d.rawSamples[0].second == 6.0);
    assert(u.rawSamples[1].second + d.rawSamples[1].second == 10.0);
    // A watcher/control mutation during update makes its snapshot stale for draw.
    auto changed = context; ++changed.feedbackGeneration;
    assert(!updates.snapshot(changed).matchesCurrentContext);
    draws.beginFrame(changed, 3, 3.0); draws.endComposedDraw(3.01);
    assert(draws.snapshot(changed).rawSamples.empty());
    updates.beginFrame(changed, 4, 4.0); updates.endComposedDraw(4.01);
    assert(updates.snapshot(changed).attempts == 1);
    assert(updates.snapshot(changed).rawSamples.empty());
}

void testFeedbackMeasurementRetainsOriginAndLateMaximum() {
    FeedbackGpuMeasurement measurement;
    measurement.attempt(1, 1.0, true, true);
    measurement.complete(1, false, 9.0);
    for (uint64_t frame = 2; frame <= 121; ++frame) {
        measurement.attempt(frame, double(frame), frame != 10, true);
        measurement.complete(frame, true, 2.0);
        measurement.complete(frame, false, 1.0);
    }
    auto state = measurement.admission(9, true, true);
    assert(state.agedPending && state.samples.samples == 112 && state.lastFrame == 121);
    assert(state.coverage == 112.0 / 113 && state.samples.p95 == 3.0);
    assert(measurement.pairs().count(10) == 0 && measurement.updateDrops == 1);
    measurement.complete(1, true, 11.0);
    assert(measurement.admission(9, true, true).lifetimeMaximum == 20.0);
    assert(measurement.pairs().count(1) == 0);
    measurement.reset();
    measurement.complete(1, false, 9.0);
    assert(measurement.pairs().empty() && measurement.attempts == 0 && !measurement.agedPending);
}

void testTaggedCompletionPrecedesLegacyEpochFiltering() {
    auto owned = std::make_unique<FakeBackend>();
    auto* query = owned.get();
    PerformanceTelemetry stream(std::move(owned));
    stream.setup();
    uint64_t completed = 0;
    stream.onGpuSample([&](uint64_t tag, uint64_t frame, double ms) {
        if (tag != 7) return;
        assert(frame == 1 && ms == 4.0);
        ++completed;
    });
    PerformanceContext context{1, 1, 800, 600, 1};
    stream.beginFrame(context, 1, 1.0, 7);
    stream.endComposedDraw(1.01);
    ++context.externalEpoch;
    stream.beginFrame(context, 2, 2.0, 8);
    stream.endComposedDraw(2.01);
    query->ready[0] = true;
    query->results[0] = 4'000'000;
    stream.refreshGpuResults();
    assert(completed == 1 && stream.snapshot(context).rawSamples.empty());
}

} // namespace

int main() {
    testFeedbackMeasurementRetainsOriginAndLateMaximum();
    testTaggedCompletionPrecedesLegacyEpochFiltering();
    testSeparateStreamsKeepRawOriginAndGeneration();
    testPortParsing();
    testStatisticsAndFrameRetention();
    testTelemetryLifecycleAndNoWait();
    testFullPoolDropsAndLateSamplesAreDiscarded();
    testUnsupportedIsTruthful();
    testLastFrameExpiresAndLongUptimeKeepsPrecision();
    testRuntimeBackendFailureClearsPendingState();
    testFilteredGpuSnapshotLatchesLateHardMaximumAndRefreshesWithoutFrameStart();
    std::cout << "Performance telemetry tests passed\n";
}
