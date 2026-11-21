#include "control/ShaderTrialPolicy.hpp"
#include <cassert>
#include <iostream>

using namespace control;

namespace {
struct FakeScene {
    int layerIdentity = 1;
    float alpha = 1.f;
    float position = 0.f;

    void externalResize() {
        alpha = 1.f;
        position = 0.f;
    }

    void deliver(InternalResizeCallbackDecision decision) {
        if (internalResizeCallbackMayRelayout(decision)) externalResize();
    }
};
}

int main() {
    assert(!hasShaderTrialCandidateFrame(40, 41));
    assert(hasShaderTrialCandidateFrame(41, 41));
    assert(!shouldCompleteShaderTrialAction(false));
    assert(shouldCompleteShaderTrialAction(true));
    assert(evaluateSceneRollbackResize(false, 1.9, 2.0, 800, 600, 800, 600) ==
        SceneRollbackResizeDecision::Wait);
    assert(evaluateSceneRollbackResize(false, 2.0, 2.0, 800, 600, 800, 600) ==
        SceneRollbackResizeDecision::SettleFromFramebuffer);
    assert(evaluateSceneRollbackResize(false, 2.0, 2.0, 1024, 768, 800, 600) ==
        SceneRollbackResizeDecision::Unrecoverable);
    const auto synchronousMatch = classifyInternalResizeCallback(true, 800, 600,
        800, 600, 800, 600, true);
    assert(synchronousMatch == InternalResizeCallbackDecision::Expected);
    assert(!internalResizeCallbackMayRelayout(synchronousMatch));
    const auto synchronousClamp = classifyInternalResizeCallback(true, 801, 601,
        800, 600, 800, 600, true);
    assert(synchronousClamp == InternalResizeCallbackDecision::SynchronousMismatch);
    assert(!internalResizeCallbackMayRelayout(synchronousClamp));
    const auto delayedMatch = classifyInternalResizeCallback(true, 800, 600,
        800, 600, 800, 600, false);
    assert(delayedMatch == InternalResizeCallbackDecision::Expected);
    assert(!internalResizeCallbackMayRelayout(delayedMatch));
    FakeScene edited;
    edited.alpha = .37f;
    edited.position = 42.f;
    edited.deliver(delayedMatch);
    assert(edited.alpha == .37f && edited.position == 42.f);
    FakeScene replaced;
    replaced.layerIdentity = 2;
    replaced.alpha = .61f;
    replaced.deliver(delayedMatch);
    assert(replaced.layerIdentity == 2 && replaced.alpha == .61f);
    const auto externalResize = classifyInternalResizeCallback(true, 800, 600,
        720, 540, 720, 540, false);
    assert(externalResize == InternalResizeCallbackDecision::External);
    assert(internalResizeCallbackMayRelayout(externalResize));
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
