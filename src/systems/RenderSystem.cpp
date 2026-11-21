#include "RenderSystem.hpp"
#include "../light/Lights.h"
#include <algorithm>

namespace {
bool hasDirectFramebuffer() {
    GLint framebuffer = -1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    if (framebuffer != 0) return false;

    // The legacy offscreen target is single-sample. Disabling multisample
    // rasterization leaves the framebuffer sample count visible to shaders.
    GLint samples = -1;
    glGetIntegerv(GL_SAMPLES, &samples);
    if (samples != 0) return false;

    for (GLenum component : {GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,
                             GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE}) {
        GLint bits = 0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK_LEFT, component, &bits);
        if (bits != 8) return false;
    }
    GLint depthBits = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH,
                                          GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthBits);
    return depthBits >= 24;
}

bool isPostPass(PostPass pass) {
    return pass >= PostPass::BLOOM && pass <= PostPass::LIMBDARKENING;
}

bool isDeferredPass(PostPass pass) {
    switch (pass) {
        case PostPass::BG:
        case PostPass::EDGE:
        case PostPass::SSAO:
        case PostPass::SHADOWLIGHT:
        case PostPass::POINTLIGHT:
        case PostPass::FXAA:
        case PostPass::FOG:
        case PostPass::DOF:
        case PostPass::BLOOM:
            return true;
        default:
            return false;
    }
}
}

void RenderSystem::setup(int initialLayers) {
    setupLayers(initialLayers);
}

void RenderSystem::shutdown() {
    layers.clear();
    resetScenePipeline();
}

void RenderSystem::setupLayers(int numLayers) {
    layers.resize(numLayers);
    for (int i = 0; i < layers.size(); i++) {
        if (layers[i] == nullptr) {
            layers[i] = make_shared<Layer>();
        }
        layers[i]->setup(i);
    }
}

void RenderSystem::layoutLayers(Layout layoutValue) {
    layout = layoutValue;
    for (int i = 0; i < layers.size(); i++) {
        layers[i]->layout(layout, i, layers.size());
    }
}

void RenderSystem::applyLayersPayload(const osc::LayersPayload& layersPayload) {
    int numLayers = layersPayload.hasLayerCount ? layersPayload.layerCount : INITIAL_LAYERS;
    setupLayers(numLayers);
    layoutLayers(layersPayload.hasLayout ? layersPayload.layout : layout);
}

void RenderSystem::resetLayers(const osc::LayersPayload& layersPayload) {
    setupLayers(0);
    applyLayersPayload(layersPayload);
}

void RenderSystem::update(const vector<TidalNote>& notes) {
    for (int i = 0; i < layers.size(); i++) {
        layers[i]->update(notes);
    }
}

void RenderSystem::releaseTargets() {
    post.reset();
#if USE_OFX_DEFERRED_SHADING
    shadowLightPass.reset();
    pointLightPass.reset();
    deferred.reset();
#endif
}

void RenderSystem::prepareTargets(bool cameraEnabled) {
    const int width = ofGetWidth();
    const int height = ofGetHeight();
#if USE_OFX_DEFERRED_SHADING
    if (cameraEnabled && deferredShading) {
        post.reset();
        if (!deferred) {
            deferred = std::make_unique<ofxDeferredProcessing>();
            ofEnableArbTex();
            deferred->init(width, height);
            ofDisableArbTex();
            for (auto pass : shadingPasses) createShadingPass(*deferred, pass);
        }
        return;
    }
    shadowLightPass.reset();
    pointLightPass.reset();
    deferred.reset();
#endif
    if (!directFramebufferSupported) directFramebufferSupported = hasDirectFramebuffer();
    // Keep the forward camera's projection and depth target. The direct resolve
    // assumes the window has the same precision and blend as the offscreen pass.
    if (cameraEnabled || !*directFramebufferSupported || ofGetStyle().blendingMode != OF_BLENDMODE_ALPHA ||
        std::any_of(shadingPasses.begin(), shadingPasses.end(), isPostPass)) {
        if (!post) {
            post = std::make_unique<ofxPostProcessing>();
            post->init(width, height);
            for (auto pass : shadingPasses) createShadingPass(*post, pass);
        }
    } else {
        post.reset();
    }
}

void RenderSystem::beginDraw(Camera& camera) {
    ofPushMatrix();
#if USE_OFX_DEFERRED_SHADING
    if (deferred) {
        if (shadowLightPass) {
            shadowLightPass->beginShadowMap(camera.getCamera());
            doDraw(camera);
            if (pointLightPass) pointLightPass->drawLights();
            shadowLightPass->endShadowMap();
        }
        deferred->begin(camera.getCamera());
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else
#endif
    if (camera.isEnabled()) {
        Lights::get().update();
        ofEnableDepthTest();
        ofEnableLighting();
        post->begin(camera.getCamera());
    } else if (post) {
        post->begin();
    } else {
        ofPushStyle();
    }
    ofClear(0, 0, 0, 0);
}

void RenderSystem::doDraw(Camera& camera) {
    if (camera.isEnabled()) {
        ofTranslate(-ofGetWidth()/2.f, -ofGetHeight()/2);
    }
    int totalVisible = 0;
    for (int i = 0; i < layers.size(); i++) {
        if (layers[i]->getVarBool("visible")) {
            totalVisible++;
        }
    }
    for (int i = 0; i < layers.size(); i++) {
        layers[i]->draw(totalVisible, &camera);
    }
}

void RenderSystem::endDraw() {
#if USE_OFX_DEFERRED_SHADING
    if (deferred) {
        if (pointLightPass) pointLightPass->drawLights();
        glDisable(GL_CULL_FACE);
        deferred->end();
    } else
#endif
    if (post) {
        post->end();
    } else {
        ofPopStyle();
        ofDisableDepthTest();
        ofPushStyle();
        ofFill();
        ofSetRectMode(OF_RECTMODE_CORNER);
        ofSetColor(255);
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        // Apply the former alpha-over-black presentation using the accumulated
        // window alpha. RGB becomes dst.rgb * dst.a, and alpha becomes
        // dst.a * dst.a + 1 - dst.a.
        glBlendFuncSeparate(GL_ZERO, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);
        ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());
        ofPopStyle();
    }
    ofDisableLighting();
    ofDisableDepthTest();
    ofPopMatrix();
}

void RenderSystem::draw(Camera& camera, bool showDebug, const std::function<void()>& afterCompose) {
    prepareTargets(camera.isEnabled());
    beginDraw(camera);
    doDraw(camera);
    endDraw();
    if (afterCompose) afterCompose();

    if (showDebug) {
        if (camera.isEnabled()) {
            ofDrawBitmapString(ofToString(camera.getPosition()), 20, 20);
        }
        ofDrawBitmapString(ofToString(ofGetFrameRate()), ofGetWidth()-100, 20);
        inspector.inspect(layers);
    }
}

void RenderSystem::resizeBacking(int w, int h) {
    if (w > 0 && h > 0) {
        directFramebufferSupported.reset();
        releaseTargets();
    }
}

void RenderSystem::windowResized(int w, int h) {
    resizeBacking(w, h);
    layoutLayers(layout);
}

void RenderSystem::allLayersCommand(const string& command, const ofxOscMessage& m) {
    for (int i = 0; i < layers.size(); i++) {
        layers[i]->oscCommand(command, m);
    }
}

bool RenderSystem::routeLayerByIndex(int idx, const string& command, const ofxOscMessage& m, osc::ResourceAction action) {
    if (idx < 0 || static_cast<size_t>(idx) >= layers.size()) {
        ofLog() << "layer index out of bounds: " << m;
        return false;
    }

    if (action == osc::ResourceAction::LAYER_SOLO) {
        for (int i = 0; i < layers.size(); i++) {
            layers[i]->setVar("visible", i == idx);
        }
    }
    else {
        layers[idx]->oscCommand(command, m);
    }
    return true;
}

#if USE_OFX_DEFERRED_SHADING
void RenderSystem::createShadingPass(ofxDeferredProcessing& processor, PostPass passId) {
    ofEnableArbTex();
    switch (passId) {
        case PostPass::BG:
            processor.createPass<ofxDeferred::BgPass>();
            break;
        case PostPass::EDGE:
            processor.createPass<ofxDeferred::EdgePass>();
            break;
        case PostPass::SSAO:
            processor.createPass<ofxDeferred::SsaoPass>();
            break;
        case PostPass::SHADOWLIGHT:
            shadowLightPass = processor.createPass<ofxDeferred::ShadowLightPass>();
            break;
        case PostPass::POINTLIGHT:
            pointLightPass = processor.createPass<ofxDeferred::PointLightPass>();
            pointLightPass->addLight();
            break;
        case PostPass::FXAA:
            processor.createPass<ofxDeferred::FxaaPass>();
            break;
        case PostPass::FOG:
            processor.createPass<ofxDeferred::FogPass>();
            break;
        case PostPass::DOF:
            processor.createPass<ofxDeferred::DofPass>();
            break;
        case PostPass::BLOOM:
            processor.createPass<ofxDeferred::BloomPass>();
            break;
        default:
            break;
    }
    ofDisableArbTex();
}

#endif

void RenderSystem::createShadingPass(ofxPostProcessing& processor, PostPass passId) {
    switch (passId) {
        case PostPass::BLOOM:
            processor.createPass<itg::BloomPass>();
            break;
        case PostPass::CONVOLUTION:
            processor.createPass<itg::ConvolutionPass>();
            break;
        case PostPass::DOF:
            processor.createPass<itg::DofPass>();
            break;
        case PostPass::DOFALT:
            processor.createPass<itg::DofAltPass>();
            break;
        case PostPass::EDGE:
            processor.createPass<itg::EdgePass>();
            break;
        case PostPass::FXAA:
            processor.createPass<itg::FxaaPass>();
            break;
        case PostPass::KALEIDOSCOPE:
            processor.createPass<itg::KaleidoscopePass>();
            break;
        case PostPass::NOISEWARP:
            processor.createPass<itg::NoiseWarpPass>();
            break;
        case PostPass::PIXELATE:
            processor.createPass<itg::PixelatePass>();
            break;
        case PostPass::LUT:
            processor.createPass<itg::LUTPass>();
            break;
        case PostPass::CONTRAST:
            processor.createPass<itg::ContrastPass>();
            break;
        case PostPass::SSAO:
            processor.createPass<itg::SSAOPass>();
            break;
        case PostPass::HTILTSHIFT:
            processor.createPass<itg::HorizontalTiltShifPass>();
            break;
        case PostPass::VTILTSHIFT:
            processor.createPass<itg::VerticalTiltShifPass>();
            break;
        case PostPass::RGBSHIFT:
            processor.createPass<itg::RGBShiftPass>();
            break;
        case PostPass::FAKESSS:
            processor.createPass<itg::FakeSSSPass>();
            break;
        case PostPass::ZOOMBLUR:
            processor.createPass<itg::ZoomBlurPass>();
            break;
        case PostPass::BLEACHBYPASS:
            processor.createPass<itg::BleachBypassPass>();
            break;
        case PostPass::TOON:
            processor.createPass<itg::ToonPass>();
            break;
        case PostPass::GODRAYS:
            processor.createPass<itg::GodRaysPass>();
            break;
        case PostPass::RIMHIGHLIGHTING:
            processor.createPass<itg::RimHighlightingPass>();
            break;
        case PostPass::LIMBDARKENING:
            processor.createPass<itg::LimbDarkeningPass>();
            break;
        default:
            break;
    }
}

void RenderSystem::setDeferredShading(bool enabled) {
#if !USE_OFX_DEFERRED_SHADING
    if (enabled) return;
#endif
    if (deferredShading == enabled) return;
    deferredShading = enabled;
    releaseTargets();
}

void RenderSystem::applyShadingPasses(const vector<osc::ShadingPassSpec>& passes) {
    vector<PostPass> requested;
    requested.reserve(passes.size());
    for (const auto& spec : passes) {
        PostPass pass = static_cast<PostPass>(spec.id);
        if (spec.byName) {
            const auto found = PostPassMap.find(spec.name);
            if (found == PostPassMap.end()) {
                ofLogError("shading") << "unknown pass " << spec.name;
                return;
            }
            pass = found->second;
        }
        if (isPostPass(pass) || isDeferredPass(pass)) requested.push_back(pass);
    }
    shadingPasses = std::move(requested);
    releaseTargets();
}

#if !USE_OFX_DEFERRED_SHADING
std::string RenderSystem::excludedShadingFeature(const osc::Command& command) {
    if (command.type == osc::CommandType::SHADING_MODE && command.shadingMode == "deferred")
        return "deferred shading";
    if (command.type == osc::CommandType::SHADING_PASSES) {
        for (const auto& spec : command.shadingPasses) {
            PostPass pass = static_cast<PostPass>(spec.id);
            if (spec.byName) {
                const auto found = PostPassMap.find(spec.name);
                if (found == PostPassMap.end()) continue;
                pass = found->second;
            }
            if (!isPostPass(pass) && isDeferredPass(pass))
                return "deferred pass " + (spec.byName ? spec.name : std::to_string(spec.id));
        }
    }
    return {};
}
#endif

void RenderSystem::resetScenePipeline() {
    deferredShading = false;
    shadingPasses.clear();
    releaseTargets();
}

#ifdef VOSC_RECOVERY_TESTING
ofJson RenderSystem::recoveryTestPipeline() const {
    const auto postCount = std::count_if(shadingPasses.begin(), shadingPasses.end(), isPostPass);
#if USE_OFX_DEFERRED_SHADING
    const auto deferredCount = std::count_if(shadingPasses.begin(), shadingPasses.end(), isDeferredPass);
    return {{"deferred", deferredShading}, {"post_passes", postCount},
            {"deferred_passes", deferredCount}, {"point_light_pass", bool(pointLightPass)},
            {"shadow_light_pass", bool(shadowLightPass)}};
#else
    return {{"deferred", false}, {"post_passes", postCount}, {"deferred_passes", 0},
            {"point_light_pass", false}, {"shadow_light_pass", false}};
#endif
}

ofJson RenderSystem::recoveryTestTargets() const {
    return {{"post", bool(post)},
#if USE_OFX_DEFERRED_SHADING
            {"deferred", bool(deferred)}};
#else
            {"deferred", false}};
#endif
}
#endif

bool RenderSystem::supportsDirectShaderTrial() {
    return layers.size() == 1 && layers.front() && supportsScenePersistence();
}

bool RenderSystem::supportsScenePersistence() {
    return !deferredShading && shadingPasses.empty();
}

void RenderSystem::replaceSceneLayers(vector<shared_ptr<Layer>> replacement, Layout replacementLayout) {
    layers = std::move(replacement);
    layout = replacementLayout;
    for (size_t index = 0; index < layers.size(); ++index) {
        if (layers[index]) layers[index]->index = static_cast<int>(index);
    }
}

void RenderSystem::toggleWireframeAll() {
    for (int i = 0; i < layers.size(); i++) {
        if (layers[i]->hasGeom()) {
            layers[i]->geom->drawWireframe = !layers[i]->geom->drawWireframe;
        }
    }
}

void RenderSystem::mousePressed(int x, int y, int button, bool showDebug) {
    if (showDebug) {
        inspector.mousePressed(x, y, button);
    }
}

void RenderSystem::mouseReleased(int x, int y, int button, bool showDebug) {
    if (showDebug) {
        inspector.mouseReleased(x, y, button);
    }
}

void RenderSystem::keyPressed(int key, bool showDebug) {
    switch (key) {
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '0': {
            if (showDebug) {
                inspector.keyPressed(key);
            }
            else {
                ofxOscMessage m;
                if (key == '0') {
                    m.addIntArg(OF_BLENDMODE_DISABLED);
                    allLayersCommand("/blendmode", m);
                }
                else {
                    m.addIntArg(static_cast<ofBlendMode>(key - '1' + 1));
                    allLayersCommand("/blendmode", m);
                }
            }
            break;
        }
        case 'r': {
            ofxOscMessage m;
            allLayersCommand("/tex/reload", m);
            break;
        }
        case 'u': {
            ofxOscMessage m;
            allLayersCommand("/layer/reset", m);
            break;
        }
        case 'w':
            toggleWireframeAll();
            break;
        default:
            break;
    }
}
