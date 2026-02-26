#include "RenderSystem.hpp"
#include "../light/Lights.h"

void RenderSystem::setup(int initialLayers) {
    setupLayers(initialLayers);
}

void RenderSystem::shutdown() {
    layers.clear();
    post.getPasses().clear();
    deferred.getPasses().clear();
    pointLightPass = nullptr;
    shadowLightPass = nullptr;
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

    if (pointLightPass != nullptr) {
        // todo: fix deferred point light synchronization
    }
}

void RenderSystem::beginDraw(Camera& camera) {
    ofPushMatrix();
    if (camera.isEnabled()) {
        if (deferredShading) {
            if (shadowLightPass != nullptr) {
                shadowLightPass->beginShadowMap(camera.getCamera());
                doDraw(camera);
                if (pointLightPass != nullptr) {
                    pointLightPass->drawLights();
                }
                shadowLightPass->endShadowMap();
            }

            deferred.begin(camera.getCamera());
        }
        else {
            Lights::get().update();

            ofEnableDepthTest();
            ofEnableLighting();

            post.begin(camera.getCamera());
        }
        if (deferredShading) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
    }
    else {
        post.begin();
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
    if (deferredShading) {
        if (pointLightPass != nullptr) {
            pointLightPass->drawLights();
        }
        glDisable(GL_CULL_FACE);
        deferred.end();
    }
    else {
        post.end();
    }
    ofDisableLighting();
    ofDisableDepthTest();
    ofPopMatrix();
}

void RenderSystem::draw(Camera& camera, bool showDebug) {
    beginDraw(camera);
    doDraw(camera);
    endDraw();

    if (showDebug) {
        if (camera.isEnabled()) {
            ofDrawBitmapString(ofToString(camera.getPosition()), 20, 20);
        }
        ofDrawBitmapString(ofToString(ofGetFrameRate()), ofGetWidth()-100, 20);
        inspector.inspect(layers);
    }
}

void RenderSystem::windowResized(int w, int h) {
    if (w > 0 && h > 0) {
        ofEnableArbTex();
        deferred.init(ofGetWidth(), ofGetHeight());
        ofDisableArbTex();

        post.init(ofGetWidth(), ofGetHeight());
    }
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

void RenderSystem::createShadingPass(ofxDeferredProcessing& processor, const string& passName) {
    createShadingPass(processor, PostPassMap.at(passName));
}

void RenderSystem::createShadingPass(ofxDeferredProcessing& processor, int passId) {
    createShadingPass(processor, static_cast<PostPass>(passId));
}

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

void RenderSystem::createShadingPass(ofxPostProcessing& processor, const string& passName) {
    if (PostPassMap.find(passName) != PostPassMap.end()) {
        createShadingPass(processor, PostPassMap.at(passName));
    }
    else {
        ofLogError("shading pass: " + passName + " does not exist");
    }
}

void RenderSystem::createShadingPass(ofxPostProcessing& processor, int passId) {
    createShadingPass(processor, static_cast<PostPass>(passId));
}

void RenderSystem::applyShadingPasses(const vector<osc::ShadingPassSpec>& passes) {
    post.getPasses().clear();
    deferred.getPasses().clear();
    shadowLightPass = nullptr;
    pointLightPass = nullptr;

    for (size_t i = 0; i < passes.size(); ++i) {
        if (passes[i].byName) {
            createShadingPass(post, passes[i].name);
            createShadingPass(deferred, passes[i].name);
        }
        else {
            createShadingPass(post, passes[i].id);
            createShadingPass(deferred, passes[i].id);
        }
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
