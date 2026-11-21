#include "ShaderTrialPolicy.hpp"

namespace control {

bool hasShaderTrialCandidateFrame(uint64_t currentFrame, uint64_t firstCandidateFrame) {
    return currentFrame >= firstCandidateFrame;
}

bool shouldCompleteShaderTrialAction(bool trialStillActive) {
    return trialStillActive;
}

SceneRollbackResizeDecision evaluateSceneRollbackResize(bool resizeSettled,
                                                        double nowSeconds,
                                                        double deadlineSeconds,
                                                        int framebufferWidth,
                                                        int framebufferHeight,
                                                        int baselineWidth,
                                                        int baselineHeight) {
    if (resizeSettled || nowSeconds < deadlineSeconds)
        return SceneRollbackResizeDecision::Wait;
    if (framebufferWidth == baselineWidth && framebufferHeight == baselineHeight)
        return SceneRollbackResizeDecision::SettleFromFramebuffer;
    return SceneRollbackResizeDecision::Unrecoverable;
}

InternalResizeCallbackDecision classifyInternalResizeCallback(bool hasExpectation,
                                                              int expectedWidth,
                                                              int expectedHeight,
                                                              int eventWidth,
                                                              int eventHeight,
                                                              int actualWidth,
                                                              int actualHeight,
                                                              bool synchronousRequest) {
    if (hasExpectation && eventWidth == expectedWidth && eventHeight == expectedHeight &&
        actualWidth == expectedWidth && actualHeight == expectedHeight)
        return InternalResizeCallbackDecision::Expected;
    if (hasExpectation && synchronousRequest)
        return InternalResizeCallbackDecision::SynchronousMismatch;
    return InternalResizeCallbackDecision::External;
}

bool internalResizeCallbackMayRelayout(InternalResizeCallbackDecision decision) {
    return decision == InternalResizeCallbackDecision::External;
}

ShaderTrialEvaluation evaluateShaderTrial(const FilteredGpuSnapshot& gpu,
                                          double budgetMilliseconds,
                                          double nowSeconds,
                                          double deadlineSeconds,
                                          uint64_t currentFrame,
                                          size_t minimumSamples,
                                          uint64_t maximumSampleAge) {
    if (nowSeconds >= deadlineSeconds)
        return {ShaderTrialDecision::Rollback, "trial_expired"};
    if (!gpu.supported)
        return {ShaderTrialDecision::Rollback, "trial_gpu_unavailable"};
    if (!gpu.matchesContext)
        return {ShaderTrialDecision::Rollback, "trial_context_changed"};
    if (gpu.lifetimeMaximum && *gpu.lifetimeMaximum > budgetMilliseconds * 2.0)
        return {ShaderTrialDecision::Rollback, "trial_budget_exceeded"};
    if (gpu.agedPending)
        return {ShaderTrialDecision::Rollback, "trial_gpu_evidence_expired"};
    if (gpu.samples.samples < minimumSamples || gpu.coverage < 0.95)
        return {ShaderTrialDecision::Evaluate, {}};
    if (!gpu.samples.p95 || *gpu.samples.p95 > budgetMilliseconds)
        return {ShaderTrialDecision::Rollback, "trial_budget_exceeded"};
    if (!gpu.lastFrame || *gpu.lastFrame > currentFrame ||
        currentFrame - *gpu.lastFrame > maximumSampleAge)
        return {ShaderTrialDecision::Evaluate, {}};
    return {ShaderTrialDecision::Ready, {}};
}

} // namespace control
