#pragma once

#include "PerformanceTelemetry.hpp"
#include <cstdint>
#include <string>

namespace control {

enum class ShaderTrialDecision { Evaluate, Ready, Rollback };
enum class SceneRollbackResizeDecision { Wait, SettleFromFramebuffer, Unrecoverable };
enum class InternalResizeCallbackDecision { Expected, SynchronousMismatch, External };

struct ShaderTrialEvaluation {
    ShaderTrialDecision decision = ShaderTrialDecision::Evaluate;
    std::string errorCode;
};

ShaderTrialEvaluation evaluateShaderTrial(const FilteredGpuSnapshot& gpu,
                                          double budgetMilliseconds,
                                          double nowSeconds,
                                          double deadlineSeconds,
                                          uint64_t currentFrame,
                                          size_t minimumSamples = 30,
                                          uint64_t maximumSampleAge = 8);
bool hasShaderTrialCandidateFrame(uint64_t currentFrame, uint64_t firstCandidateFrame);
bool shouldCompleteShaderTrialAction(bool trialStillActive);
SceneRollbackResizeDecision evaluateSceneRollbackResize(bool resizeSettled,
                                                        double nowSeconds,
                                                        double deadlineSeconds,
                                                        int framebufferWidth,
                                                        int framebufferHeight,
                                                        int baselineWidth,
                                                        int baselineHeight);
InternalResizeCallbackDecision classifyInternalResizeCallback(bool hasExpectation,
                                                              int expectedWidth,
                                                              int expectedHeight,
                                                              int eventWidth,
                                                              int eventHeight,
                                                              int actualWidth,
                                                              int actualHeight,
                                                              bool synchronousRequest);
bool internalResizeCallbackMayRelayout(InternalResizeCallbackDecision decision);

} // namespace control
