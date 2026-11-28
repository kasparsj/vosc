#pragma once

#include "ofMain.h"
#include "LocalControlServer.hpp"
#include "PerformanceTelemetry.hpp"
#include "ShaderTrialPolicy.hpp"
#include "../shader/Shader.h"
#include "../input/Inputs.hpp"
#include <set>
#include <deque>
#include <map>
#include <optional>
#ifdef VOSC_WEBGPU_M1
#include "../webgpu/GlslTranslation.hpp"
#include "../webgpu/M1Preview.hpp"
#endif

class ShaderPingPongTex;
class CommandSystem;
class RenderSystem;
struct RuntimeContext;

namespace control {

class EngineControl {
public:
    EngineControl(RenderSystem& render, CommandSystem& commands, RuntimeContext& runtime);
    void bindScene(RenderSystem& render, CommandSystem& commands, RuntimeContext& runtime);
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
    void enableDraftPreview() { draftPreview = true; }
    void enableDraftValidation() { draftValidation = true; }
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
    const Receipt* findReceipt(const std::string& id) const;
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

    RenderSystem& render() const { return *sceneRender; }
    CommandSystem& commands() const { return *sceneCommands; }
    RuntimeContext& runtime() const { return *sceneRuntime; }
    RenderSystem* sceneRender;
    CommandSystem* sceneCommands;
    RuntimeContext* sceneRuntime;
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
    struct DraftCandidate {
        Shader::DraftProgram program;
        Inputs inputs;
        ofJson slots;
        ofJson bindings;
        std::string lease, token, sourceDigest;
        uint64_t revision = 0;
        double expiresAt = 0;
        bool negotiatedBackend = false;
#ifdef VOSC_WEBGPU_M1
        std::string wgslPrepareId, webGpuSource, artifactDigest, compilerIdentity;
        std::vector<webgpu::M1Preview::ControlLane> controls;
        std::optional<float> fixedTime;
        bool needsTranslation = false;
        bool wgslStarted = false;
#endif
    };
    struct DraftDraw {
        std::string id;
        std::unique_ptr<DraftCandidate> baseline;
        ofJson previousActivation;
        uint64_t previousRevision = 0;
        uint64_t previousSampleSequence = 0;
        GLuint expectedProgram = 0;
        std::map<std::string, float> expectedSlots;
        std::vector<std::string> expectedBindings;
    };
    bool draftPreview = false;
    bool draftValidation = false;
    std::unique_ptr<DraftCandidate> draftCandidate;
    DraftDraw draftDraw;
    std::string queuedDraftCommit;
    std::set<std::string> fencedDraftLeases;
    ofJson draftActivation;
    uint64_t draftSampleSequence = 0;
    std::optional<Receipt> draftCommitReceipt;
    std::optional<Receipt> draftControlReceipt;
    bool executeDraft(const std::string& id, const std::string& method, const ofJson& params, uint64_t logStart);
    bool prepareOpenGlDraft(const std::string& id, const ofJson& params, const std::string& source,
                            std::unique_ptr<DraftCandidate> candidate, uint64_t logStart);
    bool settleDraftDraw();
    void discardDraftCandidate(const char* code = "stale_candidate");
#ifdef VOSC_WEBGPU_M1
    ofJson m1Status() const;
    void drawM1Preview();
    void completeM1OpenGlHandoff();
    bool draftWebGpuActive() const;
    webgpu::M1Preview* draftWebGpuContext() const;
    bool pollDraftTranslation();
    void pollDraftWebGpuPrepare();
    webgpu::GlslTranslationTools translationTools = webgpu::GlslTranslationTools::fromEnvironment();
    webgpu::GlslTranslationJob translationJob;
    std::string translationRequest;
    bool settleDraftWebGpuDraw();
    std::unique_ptr<webgpu::M1Preview> draftWebGpuStaging;
    std::string draftWebGpuCommit;
    uint64_t draftWebGpuFrame = 0;
    std::unique_ptr<webgpu::M1Preview> m1Preview;
    std::string m1Switch;
    uint64_t m1OpenGlFrame = 0;
    bool m1OpenGlStaged = false;
#endif
    void observeSelfFeedbackDraw();
    ofJson selfFeedbackDraw;
    ofJson graphDraw;
    std::unique_ptr<Trial> trial;
#ifdef VOSC_RECOVERY_TESTING
    void recoveryTestCaptureGraph(const char* phase, const ofJson& previous, uint64_t heldStep);
    bool recoveryTestGraphHeld = false;
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
