#include "control/ShaderTrialPolicy.hpp"
#include <cassert>
#include <iostream>

using namespace control;

int main() {
    assert(!hasShaderTrialCandidateFrame(40, 41));
    assert(hasShaderTrialCandidateFrame(41, 41));
    FilteredGpuSnapshot gpu;
    gpu.supported = gpu.matchesContext = true;
    gpu.samples.samples = 30;
    gpu.samples.p95 = 7.0;
    gpu.lifetimeMaximum = 9.0;
    gpu.lastFrame = 40;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).decision == ShaderTrialDecision::Ready);
    assert(evaluateShaderTrial(gpu, 8.0, 2.0, 2.0, 40).errorCode == "trial_expired");
    gpu.lifetimeMaximum = 16.01;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).errorCode == "trial_budget_exceeded");
    gpu.lifetimeMaximum = 9.0;
    gpu.lastFrame = 31;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).decision == ShaderTrialDecision::Evaluate);
    gpu.lastFrame = 40;
    gpu.matchesContext = false;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).errorCode == "trial_context_changed");
    gpu.matchesContext = true;
    gpu.supported = false;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).errorCode == "trial_gpu_unavailable");
    gpu.supported = true;
    gpu.samples.samples = 29;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).decision == ShaderTrialDecision::Evaluate);
    gpu.samples.samples = 30;
    gpu.coverage = .949;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).decision == ShaderTrialDecision::Evaluate);
    gpu.coverage = .95;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).decision == ShaderTrialDecision::Ready);
    gpu.agedPending = true;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).errorCode == "trial_gpu_evidence_expired");
    assert(evaluateShaderTrial(gpu, 8.0, 2.0, 2.0, 40).errorCode == "trial_expired");
    gpu.agedPending = false;
    gpu.samples.p95 = 8.01;
    assert(evaluateShaderTrial(gpu, 8.0, 1.0, 2.0, 40).errorCode == "trial_budget_exceeded");
    std::cout << "Shader trial policy tests passed\n";
}
