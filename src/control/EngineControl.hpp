#pragma once

#include "ofMain.h"
#include "LocalControlServer.hpp"
#include "PerformanceTelemetry.hpp"
#include "ShaderTrialPolicy.hpp"
#include "../shader/Shader.h"
#include <deque>
#include <map>
#include <optional>

class ShaderPingPongTex;
class CommandSystem;
class RenderSystem;
struct RuntimeContext;

namespace control {

class EngineControl {
public:
    EngineControl(RenderSystem& render, CommandSystem& commands, RuntimeContext& runtime);
    void setup();
    void update();
    void beforeUpdate();
    void afterUpdate();
    void beforeDraw();
    void afterComposedDraw();
    void afterDraw();
    void shutdown();
    void noteExternalChange() { ++externalEpoch; ++configurationEpoch; ++feedbackConfigurationEpoch; }
    void noteExternalSample() { ++externalEpoch; }
    void noteControlAdmission() { ++configurationEpoch; }
    void beforeLegacyControl(bool inputSample);
    void interruptTrial();
    bool canBeginSceneReplay() const { return !trial && pendingCount() == 0 && queued.empty() && captures.empty(); }
    uint64_t configurationSerial() const { return revision ^ (configurationEpoch << 32); }
#ifdef VOSC_RECOVERY_TESTING
    ofJson recoveryTestSnapshot() const;
    ofJson recoveryTestConflictFeedbackTrial(const ofJson& command);
    ofJson recoveryTestConfigureFeedbackTrial(const ofJson& command);
    const ofJson& recoveryTestFeedbackCaptureReceipt() const { return recoveryTestFeedbackCaptureResult; }
    uint64_t recoveryTestFeedbackUpdates() const;
    ofJson recoveryTestInspectPrograms(const std::vector<unsigned>& programs) const;
    ofJson recoveryTestDispatch(const ofJson& request) { return dispatch(request); }
#endif

private:
    struct Receipt {
        ofJson request;
        ofJson response;
        double acceptedAt;
        std::string feedbackPreviewOwner;
    };
    struct Trial {
        std::string id;
        std::string actionId;
        size_t layer = 0;
        double budgetMilliseconds = 8.0;
        double deadlineSeconds = 0.0;
        double rollbackDeadlineSeconds = 0.0;
        uint64_t evaluationRevision = 0;
        uint64_t firstFrame = 0;
        uint64_t candidateProgram = 0;
        std::string phase = "evaluating";
        std::string method = "trial_shader";
        std::string rollbackCode;
        Shader::DirectSourceState baseline;
        PerformanceContext context;
        FilteredGpuSnapshot metrics;
        std::weak_ptr<Texture> feedbackHolder;
        std::shared_ptr<ShaderPingPongTex> feedbackBaseline;
        std::shared_ptr<ShaderPingPongTex> feedbackCandidate;
        bool feedbackActivated = false;
        uint64_t baselineProgram = 0;
        uint64_t updatesBefore = 0;
        uint64_t updatedFrame = 0;
        uint64_t candidateDrawFrame = 0;
        uint64_t restorationRevision = 0;
        uint64_t measurementId = 0;
    };
    std::string handle(const std::string& line);
    ofJson dispatch(const ofJson& request);
    void validate(const std::string& method, const ofJson& params) const;
    void execute(const std::string& id);
    ofJson scene() const;
    ofJson capabilities() const;
    ofJson diagnostics(uint64_t after, size_t limit) const;
    ofJson performance() const;
    PerformanceContext performanceContext() const;
    struct FeedbackContext {
        uint64_t configuration = 0;
        uint64_t generation = 0;
        uint64_t owner = 0;
        unsigned width = 0;
        unsigned height = 0;
        GLuint program = 0;
        bool operator==(const FeedbackContext& other) const {
            return configuration == other.configuration && generation == other.generation &&
                owner == other.owner && width == other.width && height == other.height && program == other.program;
        }
    };
    FeedbackContext feedbackContext(const PerformanceContext& current) const;
    bool feedbackTimersSupported() const;
    ofJson feedbackPerformance(const PerformanceContext& current) const;
    ofJson reply(const ofJson& id, const std::string& status) const;
    ofJson failure(const ofJson& id, const std::string& code,
                   const std::string& message, const ofJson& diagnosticEntries = ofJson::array()) const;
    bool makeReceiptSpace();
    void capture(const std::string& id);
    size_t pendingCount() const;
    bool isPinned(const std::string& id) const;
    void startTrial(const std::string& id);
    void startFeedbackTrial(Trial& active, const ofJson& params);
    std::shared_ptr<Texture> ownedFeedbackHolder() const;
    ShaderPingPongTex& expectedFeedback() const;
    bool checkFeedbackOwnership();
    void settleFeedbackDraw();
    void evaluateTrial();
    void rollbackTrial(const std::string& code);
    void updateTrialReceipt();
    ofJson trialSummary() const;

    RenderSystem& render;
    CommandSystem& commands;
    RuntimeContext& runtime;
    LocalControlServer server;
    PerformanceTelemetry telemetry;
    PerformanceTelemetry updateTelemetry;
    FeedbackGpuMeasurement feedbackMeasurement;
    FeedbackContext measurementContext;
    uint64_t measurementId = 0;
    uint64_t feedbackConfigurationEpoch = 0;
    ofJson feedbackPreviewDraw;
    uint64_t crossingFrames = 0;
    uint64_t lastCrossingFrame = 0;
    bool measurementStale = false;
    bool submittedFeedbackUpdate = false;
    std::map<std::string, Receipt> receipts;
    std::deque<std::string> receiptOrder;
    std::deque<std::string> queued;
    std::deque<std::string> captures;
    std::deque<std::string> images;
    uint64_t revision = 0;
    uint64_t renderedRevision = 0;
    uint64_t frame = 0;
    uint64_t externalEpoch = 0;
    uint64_t configurationEpoch = 0;
    std::unique_ptr<Trial> trial;
#ifdef VOSC_RECOVERY_TESTING
    std::optional<double> recoveryTestPreviousDrawStart;
    std::optional<double> recoveryTestDrawInterval;
    std::string recoveryTestFeedbackCapture;
    ofJson recoveryTestFeedbackCaptureResult;
    void recoveryTestCaptureFeedback(std::string phase, ShaderPingPongTex& texture);
    bool recoveryTestFeedbackTimerLost = false;
    bool recoveryTestSuppressFeedbackPairs = false;
#endif
};

} // namespace control
