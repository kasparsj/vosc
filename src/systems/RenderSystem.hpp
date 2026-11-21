#pragma once

#include "../Camera.hpp"
#include "../layer/Layer.h"
#include "../inspector/Inspector.hpp"
#include "../osc/Command.hpp"
#include "ofxDeferredShading.h"
#include "ofxPostProcessing.h"

class RenderSystem {
public:
    void setup(int initialLayers);
    void shutdown();

    void update(const vector<TidalNote>& notes);
    void draw(Camera& camera, bool showDebug, const std::function<void()>& afterCompose = {});
    void resizeBacking(int w, int h);
    void windowResized(int w, int h);

    void applyLayersPayload(const osc::LayersPayload& layersPayload);
    void resetLayers(const osc::LayersPayload& layersPayload);
    void layoutLayers(Layout layout);
    Layout getLayout() const {
        return layout;
    }

    void setDeferredShading(bool enabled) {
        deferredShading = enabled;
    }
    void applyShadingPasses(const vector<osc::ShadingPassSpec>& passes);
    void resetScenePipeline() {
        deferredShading = false;
        applyShadingPasses({});
    }
#ifdef VOSC_RECOVERY_TESTING
    ofJson recoveryTestPipeline() {
        return {{"deferred", deferredShading}, {"post_passes", post.getPasses().size()},
                {"deferred_passes", deferred.getPasses().size()},
                {"point_light_pass", bool(pointLightPass)}, {"shadow_light_pass", bool(shadowLightPass)}};
    }
#endif
    bool supportsDirectShaderTrial();
    bool supportsScenePersistence();
    void replaceSceneLayers(vector<shared_ptr<Layer>> replacement, Layout replacementLayout);

    void allLayersCommand(const string& command, const ofxOscMessage& m);
    bool routeLayerByIndex(int idx, const string& command, const ofxOscMessage& m, osc::ResourceAction action);

    void mousePressed(int x, int y, int button, bool showDebug);
    void mouseReleased(int x, int y, int button, bool showDebug);
    void keyPressed(int key, bool showDebug);

    vector<shared_ptr<Layer>>& getLayers() {
        return layers;
    }
    const vector<shared_ptr<Layer>>& getLayers() const {
        return layers;
    }

private:
    void setupLayers(int numLayers);
    void beginDraw(Camera& camera);
    void doDraw(Camera& camera);
    void endDraw();
    void toggleWireframeAll();
    void createShadingPass(ofxDeferredProcessing& processor, PostPass passId);
    void createShadingPass(ofxDeferredProcessing& processor, const string& passName);
    void createShadingPass(ofxDeferredProcessing& processor, int passId);
    void createShadingPass(ofxPostProcessing& processor, PostPass passId);
    void createShadingPass(ofxPostProcessing& processor, const string& passName);
    void createShadingPass(ofxPostProcessing& processor, int passId);

    vector<shared_ptr<Layer>> layers;
    Layout layout = Layout::STACK;
    Inspector inspector;
    ofxPostProcessing post;
    ofxDeferredProcessing deferred;
    bool deferredShading = false;
    ofxDeferred::PointLightPass::Ptr pointLightPass;
    ofxDeferred::ShadowLightPass::Ptr shadowLightPass;
};
