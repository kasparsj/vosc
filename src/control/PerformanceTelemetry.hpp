#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace control {

struct PerformanceContext {
    uint64_t revision = 0;
    uint64_t externalEpoch = 0;
    unsigned width = 0;
    unsigned height = 0;
    uint64_t feedbackGeneration = 0;

    bool operator==(const PerformanceContext& other) const;
    bool operator!=(const PerformanceContext& other) const { return !(*this == other); }
};

struct PerformanceStats {
    size_t samples = 0;
    std::optional<double> mean;
    std::optional<double> p50;
    std::optional<double> p95;
    std::optional<double> maximum;
};

class PerformanceSampleWindow {
public:
    static constexpr size_t Capacity = 120;
    void add(uint64_t frame, double milliseconds);
    void advanceTo(uint64_t newestFrame);
    void clear();
    bool empty() const { return values.empty(); }
    PerformanceStats stats() const;

private:
    struct Sample { uint64_t frame; double milliseconds; };
    std::deque<Sample> values;
    bool hasNewestFrame = false;
    uint64_t newestFrame = 0;
};

class GpuQueryBackend {
public:
    static constexpr size_t Capacity = 8;
    virtual ~GpuQueryBackend() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool supported() const = 0;
    virtual std::string reason() const = 0;
    virtual bool begin(size_t slot) = 0;
    virtual void end() = 0;
    virtual bool available(size_t slot) = 0;
    virtual uint64_t resultNanoseconds(size_t slot) = 0;
};

std::unique_ptr<GpuQueryBackend> makeOpenGlQueryBackend();

struct PerformanceSnapshot {
    bool hasContext = false;
    PerformanceContext context;
    bool matchesCurrentContext = false;
    PerformanceStats frameInterval;
    PerformanceStats cpuSubmit;
    PerformanceStats gpu;
    bool gpuSupported = false;
    std::string gpuState;
    std::optional<std::string> gpuReason;
    std::optional<uint64_t> gpuLastFrame;
    size_t pendingQueries = 0;
    uint64_t droppedSamples = 0;
    uint64_t attempts = 0;
    uint64_t lastAttemptFrame = 0;
    std::deque<std::pair<uint64_t, double>> rawSamples;
};

struct FilteredGpuSnapshot {
    bool supported = false;
    bool matchesContext = false;
    PerformanceStats samples;
    std::optional<double> lifetimeMaximum;
    std::optional<uint64_t> lastFrame;
    double coverage = 1.0;
    bool agedPending = false;
};

class PerformanceTelemetry {
public:
    explicit PerformanceTelemetry(std::unique_ptr<GpuQueryBackend> backend);
    ~PerformanceTelemetry();

    void setup();
    void shutdown();
    bool beginFrame(const PerformanceContext& context, uint64_t frame, double monotonicSeconds,
                    uint64_t measurement = 0);
    void onGpuSample(std::function<void(uint64_t, uint64_t, double)> observe) {
        gpuObserver = std::move(observe);
    }
    void endComposedDraw(double monotonicSeconds);
    void refreshGpuResults();
    bool gpuSupported() const { return initialized && backend->supported(); }
    PerformanceSnapshot snapshot(const PerformanceContext& current) const;
    FilteredGpuSnapshot filteredGpuSnapshot(const PerformanceContext& expected,
                                            uint64_t firstFrame,
                                            uint64_t warmupFrames) const;

private:
    struct Pending {
        bool active = false;
        uint64_t generation = 0;
        PerformanceContext context;
        uint64_t frame = 0;
        uint64_t measurement = 0;
    };

    void resetWindow(const PerformanceContext& context);
    size_t pendingCount() const;

    std::function<void(uint64_t, uint64_t, double)> gpuObserver;
    std::unique_ptr<GpuQueryBackend> backend;
    bool initialized = false;
    bool hasContext = false;
    PerformanceContext context;
    uint64_t generation = 0;
    uint64_t newestFrame = 0;
    std::optional<double> previousStart;
    std::optional<double> cpuStart;
    std::optional<size_t> currentQuery;
    std::array<Pending, GpuQueryBackend::Capacity> pending{};
    PerformanceSampleWindow frameIntervals;
    PerformanceSampleWindow cpuSubmissions;
    PerformanceSampleWindow gpuTimes;
    std::deque<std::pair<uint64_t, double>> gpuSamples;
    std::optional<double> gpuLifetimeMaximum;
    std::optional<uint64_t> gpuLastFrame;
    uint64_t droppedSamples = 0;
    uint64_t attempts = 0;
};

// Tagged completions from the existing query pools. A pending entry exists
// only when both submissions succeeded and at least one result is outstanding.
class FeedbackGpuMeasurement {
public:
    struct Pair { double update; double draw; };
    struct Attempt { uint64_t frame; std::optional<double> interval; bool update; bool draw; };
    void reset();
    void attempt(uint64_t frame, double now, bool update, bool draw);
    void complete(uint64_t frame, bool draw, double milliseconds);
    FilteredGpuSnapshot admission(uint64_t firstMeasured, bool supported, bool matches) const;

    const std::map<uint64_t, Pair>& pairs() const { return results; }
    const std::deque<Attempt>& window() const { return recent; }
    uint64_t attempts = 0;
    uint64_t updateDrops = 0;
    uint64_t drawDrops = 0;
    bool agedPending = false;

private:
    struct Halves { std::optional<double> update; std::optional<double> draw; };
    std::map<uint64_t, Halves> pending;
    std::map<uint64_t, Pair> results;
    std::deque<Attempt> recent;
    std::optional<double> previousStart;
    std::optional<double> maximum;
    uint64_t oldest = 0;
};

} // namespace control
