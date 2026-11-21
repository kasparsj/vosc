#pragma once

#include "PerformanceTelemetry.hpp"
#include <cstdint>
#include <string>

namespace control {

enum class ShaderTrialDecision { Evaluate, Ready, Rollback };
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
} // namespace control
