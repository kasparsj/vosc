#pragma once

#include "../Camera.hpp"
#include "../layer/Layer.h"
#include "../osc/Command.hpp"
#include "../Config.h"
#if USE_OFX_DEFERRED_SHADING
#include "ofxDeferredShading.h"
#endif
#include "ofxPostProcessing.h"
#include <optional>

class RenderSystem {
public:
    void setup(int initialLayers);
    void shutdown();

    void update(const vector<TidalNote>& notes);
    void draw(Camera& camera, const std::function<void()>& afterCompose = {},
              std::optional<bool> directDestination = std::nullopt);
    static bool canDrawDirectToWindow();
    void resizeBacking(int w, int h);
    void windowResized(int w, int h);

    void applyLayersPayload(const osc::LayersPayload& layersPayload);
    void resetLayers(const osc::LayersPayload& layersPayload);
    void layoutLayers(Layout layout);
    Layout getLayout() const {
        return layout;
    }

    void setDeferredShading(bool enabled);
    void applyShadingPasses(const vector<osc::ShadingPassSpec>& passes);
    void resetScenePipeline();
#if !USE_OFX_DEFERRED_SHADING
    static std::string excludedShadingFeature(const osc::Command& command);
#endif
#ifdef VOSC_RECOVERY_TESTING
    ofJson recoveryTestPipeline() const;
    ofJson recoveryTestTargets() const;
#endif
    bool supportsDirectShaderTrial();
    bool supportsScenePersistence();
    void replaceSceneLayers(vector<shared_ptr<Layer>> replacement, Layout replacementLayout);

    void allLayersCommand(const string& command, const ofxOscMessage& m);
    bool routeLayerByIndex(int idx, const string& command, const ofxOscMessage& m, osc::ResourceAction action);

    void keyPressed(int key);

    vector<shared_ptr<Layer>>& getLayers() {
        return layers;
    }
    const vector<shared_ptr<Layer>>& getLayers() const {
        return layers;
    }

private:
    struct DrawValidation;
    void setupLayers(int numLayers);
    void beginDraw(Camera& camera, DrawValidation& validation);
    void doDraw(Camera& camera);
    void endDraw(DrawValidation& validation);
    void toggleWireframeAll();
    void releaseTargets();
    void prepareTargets(bool cameraEnabled, std::optional<bool> directDestination);
    void createShadingPass(ofxPostProcessing& processor, PostPass passId);
#if USE_OFX_DEFERRED_SHADING
    void createShadingPass(ofxDeferredProcessing& processor, PostPass passId);
#endif

    vector<shared_ptr<Layer>> layers;
    Layout layout = Layout::STACK;
    vector<PostPass> shadingPasses;
    bool deferredShading = false;
    std::unique_ptr<ofxPostProcessing> post;
    std::optional<bool> directFramebufferSupported;
#if USE_OFX_DEFERRED_SHADING
    std::unique_ptr<ofxDeferredProcessing> deferred;
    ofxDeferred::PointLightPass::Ptr pointLightPass;
    ofxDeferred::ShadowLightPass::Ptr shadowLightPass;
#endif
};
