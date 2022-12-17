#include "VOSC.hpp"
#include "Args.h"
#include "VariablePool.h"
#include "ofxHPVPlayer.h"
#include "TexturePool.h"
#include "GeomPool.h"
#include "ShaderTex.h"
#include "Lights.h"

#ifndef TARGET_CPU_UNIVERSAL
#include "ofxUltralight.h"
#endif

void VOSC::setup(unsigned int port) {
    receiver.setup(port);
    camera.setup();
    setupLayers(INITIAL_VISUALS);
    tidal = new ofxTidalCycles(1);
    
    windowResized(ofGetWidth(), ofGetHeight());
    
#ifdef TARGET_CPU_UNIVERSAL
    ofLog() << ("ARM64: Yes");
#else
    ofLog() << ("ARM64: No");
#endif
    ofLog() << ("Vendor :" + ofToString(glGetString(GL_VENDOR)));
    ofLog() << ("GPU : " + ofToString(glGetString(GL_RENDERER)));
    ofLog() << ("OpenGL ver. " + ofToString(glGetString(GL_VERSION)));
    ofLog() << ("GLSL ver. " + ofToString(glGetString(GL_SHADING_LANGUAGE_VERSION)));
}

void VOSC::setupLayers(int numVisuals) {
    layers.resize(numVisuals);
    for (int i=0; i<layers.size(); i++) {
        if (layers[i] == NULL) {
            layers[i] = new Layer();
        }
        layers[i]->setup(i);
    }
}

void VOSC::layoutLayers(Layout layout) {
    this->layout = layout;
    for (int i=0; i<layers.size(); i++) {
        // todo: add support for animation
        layers[i]->layout(layout, i, layers.size());
    }
}

void VOSC::update() {
    camera.preUpdate();
    parseMessages();
    for (int i=0; i<inputs.size(); i++) {
        inputs[i].update();
    }
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
    VariablePool::update(inputs, tidal->notes);
    TexturePool::update(inputs, tidal->notes);
    GeomPool::update();
    for (int i = 0; i < layers.size(); i++) {
        layers[i]->update(inputs, tidal->notes);
    }
    camera.update();
    HPV::Update();
#ifndef TARGET_CPU_UNIVERSAL
    ofxUltralight::update();
#endif
}

void VOSC::draw() {
    beginDraw();
    
    int totalVisible = 0;
    for (int i=0; i<layers.size(); i++) {
        if (layers[i]->getVarBool("visible")) {
            totalVisible++;
        }
    }
    for (int i=0; i<layers.size(); i++) {
        layers[i]->draw(totalVisible);
    }
    
    endDraw();
    
    if (showDebug) {
        if (camera.isEnabled()) {
            ofDrawBitmapString(ofToString(camera.getPosition()), 20, 20);
        }
        ofDrawBitmapString(ofToString(ofGetFrameRate()), ofGetWidth()-100, 20);

        inspector.inspect(layers, inputs);
    }
}

void VOSC::beginDraw() {
    ofPushMatrix();
    if (camera.isEnabled()) {
        ofEnableDepthTest();
        ofEnableLighting();
        post.begin(camera.getCamera());
        ofTranslate(-ofGetWidth()/2.f, -ofGetHeight()/2);
    }
    else {
        post.begin();
    }
    ofClear(0, 0, 0, 0);
}

void VOSC::endDraw() {
    post.end();
    ofDisableLighting();
    ofDisableDepthTest();
    ofPopMatrix();
}

void VOSC::parseMessages(){
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage m;
        receiver.getNextMessage(m);
        parseMessage(m);
    }
    if (waitOnset < 1 || forceOnset || checkOnset()) {
        processQueue();
        forceOnset = false;
    }
}

bool VOSC::checkOnset() {
    bool isOnset;
    if (tidal->notes.size()) {
        isOnset = true;
    }
    else {
        for (int i=0; i<inputs.size(); i++) {
            if (inputs[i].onset) {
                isOnset = true;
                break;
            }
        }
    }
    return isOnset;
}

void VOSC::parseMessage(const ofxOscMessage &m) {
    string command = m.getAddress();
    if (command == "/inputs") {
        inputs.resize(m.getArgAsInt(0));
    }
    if (command == "/inputs/list") {
        vector<ofSoundDevice> devices = ofSoundStreamListDevices();
        ofLog() << ("ofSoundStreamListDevices:");
        for (int i=0; i<devices.size(); i++) {
            ofLog() << (ofToString(i) + ": " + devices[i].name);
        }
    }
    else if (command.substr(0, 6) == "/input") {
        inputsCommand(command, m);
    }
    else if (command.substr(0, 5) == "/midi") {
        midiCommand(command, m);
    }
    else if (command == "/dirt/play") {
        tidal->parse(m);
        if (waitOnset == -1) {
            waitOnset = 1;
        }
    }
    else if (command == "/onset") {
        waitOnset = m.getNumArgs() > 0 ? m.getArgAsBool(0) : (int) !(bool)waitOnset;
    }
    else if (command == "/onset/force") {
        forceOnset = true;
    }
    else if (command.substr(0, 7) == "/layers") {
        layersCommand(command, m);
    }
    else if (command.substr(0, 4) == "/cam") {
        camera.oscCommand(command, m);
    }
    else if (command.substr(0, 6) == "/light") {
        lightCommand(command, m);
    }
    else {
        messageQueue.push_back(m);
    }
}

void VOSC::processQueue() {
    while (messageQueue.size()) {
        ofxOscMessage &m = messageQueue[0];
        string command = m.getAddress();
        if (command.substr(0, 5) == "/post") {
            post.getPasses().clear();
            for (int i=0; i<m.getNumArgs(); i++) {
                if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
                    string passName = m.getArgAsString(i);
                    createPostPass(passName);
                }
                else {
                    int passId = m.getArgAsInt(i);
                    createPostPass(passId);
                }
            }
        }
        else {
            if (m.getArgType(0) == OFXOSC_TYPE_STRING) {
                string which = m.getArgAsString(0);
                bool all = which == "*" || which == "x" || which == "a";
                if (all) {
                    allLayersCommand(command, m);
                }
                else if (command.substr(0, 4) == "/tex") {
                    shared_ptr<Texture>& tex = TexturePool::getShared(which, true);
                    tex->oscCommand(command, m);
                    const glm::vec2& size = tex->data.getSize();
                    if (size.x == 0 && size.y == 0) {
                        tex->data.setSize(ofGetScreenWidth(), ofGetScreenHeight());
                    }
                }
                else if (command.substr(0, 4) == "/var") {
                    VariablePool::createOrUpdateShared(which, m, 1);
                }
                else if (command.substr(0, 5) == "/geom") {
                    GeomPool::getShared(which, true)->oscCommand(command, m);
                }
                else {
                    invalidCommand(m);
                }
            }
            else if (m.getArgType(0) == OFXOSC_TYPE_INT32) {
                int idx = m.getArgAsInt(0);
                if (idx > -1 && layers.size() > idx) {
                    layers[idx]->oscCommand(command, m);
                }
                else {
                    ofLog() << "layer index out of bounds: " << m;
                }
            }
            else {
                invalidCommand(m);
            }
        }
        messageQueue.erase(messageQueue.begin());
    }
}

void VOSC::invalidCommand(const ofxOscMessage& m) {
    ofLogError() << "command not recognized: " << m;
    for (int i=0; i<m.getNumArgs(); i++) {
        if (m.getArgType(i) == OFXOSC_TYPE_BLOB) {
            ofLog() << m.getArgAsBlob(i);
        }
    }
}

Layout parseLayout(const ofxOscMessage &m, int idx)
{
    Layout layout;
    if (m.getArgType(idx) == 's') {
        auto it = LayoutMap.find(m.getArgAsString(idx));
        if (it != LayoutMap.end()) {
            layout = it->second;
        }
    }
    else {
        layout = static_cast<Layout>(m.getArgAsInt(idx));
    }
    return layout;
}

void VOSC::layersCommand(string command, const ofxOscMessage& m) {
    if (command == "/layers") {
        setupLayers(m.getArgAsInt(0));
        if (m.getNumArgs() > 1) {
            layoutLayers(parseLayout(m, 1));
        }
    }
    else if (command == "/layers/layout") {
        vector<Layer*> layers;
        if (m.getNumArgs() > 1) {
            layers.resize(m.getNumArgs()-1);
            for (int i=0; i<m.getNumArgs()-1; i++) {
                layers[i] = this->layers[m.getArgAsInt(1+i)];
            }
        }
        else {
            layers = this->layers;
        }
        layoutLayers(parseLayout(m, 0));
    }
}

void VOSC::lightCommand(string command, const ofxOscMessage& m) {
    if (command == "/light") {
        Lights::get().create(m);
    }
    else if (command == "/light/remove") {
        Lights::get().remove(m);
    }
}

void VOSC::allLayersCommand(string command, const ofxOscMessage &m) {
    for (int i=0; i<layers.size(); i++) {
        layers[i]->oscCommand(command, m);
    }
}

struct retVals { bool all; int idx; };
retVals parseIndex(const ofxOscMessage &m) {
    bool all = false;
    int idx = -1;
    if (m.getArgType(0) == OFXOSC_TYPE_STRING) {
        string which = m.getArgAsString(0);
        all = which == "*" || which == "x" || which == "a";
        if (!all) {
            try {
                idx = std::stoi(which);
            }
            catch (...) {
                ofLogError() << ("invalid layer index " + which);
            }
        }
    }
    else {
        idx = m.getArgAsInt(0);
    }
    return retVals { all, idx };
}

void VOSC::inputsCommand(string command, const ofxOscMessage &m) {
    auto [all, idx] = parseIndex(m);
    if (all) {
        for (int i=0; i<inputs.size(); i++) {
            inputs[i].oscCommand(command, m);
        }
    }
    else if (idx >= 0) {
        if (!all && (idx+1) > inputs.size()) {
            inputs.resize(idx+1);
        }
        OSCInput& input = inputs[idx];
        input.oscCommand(command, m);
        if (command == "/input/data") {
            if (waitOnset == -1) {
                waitOnset = 1;
            }
        }
    }
    else {
        ofLogError() << ("invalid input index: " + ofToString(idx));
    }
}

void VOSC::midiCommand(string command, const ofxOscMessage &m) {
    if (command == "/midi") {
        
    }
    else if (command == "/midi/list") {
        vector<string> inPorts = midiIn.getInPortList();
        ofLog() << ("MIDI in ports:");
        for (int i=0; i<inPorts.size(); i++) {
            ofLog() << (ofToString(i) + ": " + inPorts[i]);
        }
    }
}

void VOSC::createPostPass(string passName) {
    if (passName == "bloom") {
        post.createPass<itg::BloomPass>();
    }
    else if (passName == "convolution") {
        post.createPass<itg::ConvolutionPass>();
    }
    else if (passName == "dof") {
        post.createPass<itg::DofPass>();
    }
    else if (passName == "dofalt") {
        post.createPass<itg::DofAltPass>();
    }
    else if (passName == "edge") {
        post.createPass<itg::EdgePass>();
    }
    else if (passName == "fxaa") {
        post.createPass<itg::FxaaPass>();
    }
    else if (passName == "kaleidoscope") {
        post.createPass<itg::KaleidoscopePass>();
    }
    else if (passName == "noisewarp") {
        post.createPass<itg::NoiseWarpPass>();
    }
    else if (passName == "pixelate") {
        post.createPass<itg::PixelatePass>();
    }
    else if (passName == "lut") {
        post.createPass<itg::LUTPass>();
    }
    else if (passName == "contrast") {
        post.createPass<itg::ContrastPass>();
    }
    else if (passName == "ssao") {
        post.createPass<itg::SSAOPass>();
    }
    else if (passName == "htiltshift") {
        post.createPass<itg::HorizontalTiltShifPass>();
    }
    else if (passName == "vtiltshift") {
        post.createPass<itg::VerticalTiltShifPass>();
    }
    else if (passName == "rgbshift") {
        post.createPass<itg::RGBShiftPass>();
    }
    else if (passName == "fakesss") {
        post.createPass<itg::FakeSSSPass>();
    }
    else if (passName == "zoomblur") {
        post.createPass<itg::ZoomBlurPass>();
    }
    else if (passName == "bleachbypass") {
        post.createPass<itg::BleachBypassPass>();
    }
    else if (passName == "toon") {
        post.createPass<itg::ToonPass>();
    }
    else if (passName == "godrays") {
        post.createPass<itg::GodRaysPass>();
    }
    else if (passName == "rimhighlighting") {
        post.createPass<itg::RimHighlightingPass>();
    }
    else if (passName == "limbdarkening") {
        post.createPass<itg::LimbDarkeningPass>();
    }
//    else if (passName == "invert") {
//        post.createPass<itg::Invert>();
//    }
//    else if (passName == "glitch") {
//        post.createPass<itg::Glitch>();
//    }
//    else if (passName == "rotate") {
//        post.createPass<itg::Rotate>();
//    }
//    else if (passName == "pixelsort") {
//        post.createPass<itg::Pixelsort>();
//    }
//    else if (passName == "beyoon") {
//        post.createPass<itg::Beyoon>();
//    }
//    else if (passName == "reflectx") {
//        post.createPass<itg::ReflectX>();
//    }
//    else if (passName == "reflecty") {
//        post.createPass<itg::ReflectY>();
//    }
//    else if (passName == "split") {
//        post.createPass<itg::Split>();
//    }
}

void VOSC::createPostPass(int passId) {
    switch (static_cast<PostPass>(passId)) {
        case PostPass::BLOOM:
            post.createPass<itg::BloomPass>();
            break;
        case PostPass::CONVOLUTION:
            post.createPass<itg::ConvolutionPass>();
            break;
        case PostPass::DOF:
            post.createPass<itg::DofPass>();
            break;
        case PostPass::DOFALT:
            post.createPass<itg::DofAltPass>();
            break;
        case PostPass::EDGE:
            post.createPass<itg::EdgePass>();
            break;
        case PostPass::FXAA:
            post.createPass<itg::FxaaPass>();
            break;
        case PostPass::KALEIDOSCOPE:
            post.createPass<itg::KaleidoscopePass>();
            break;
        case PostPass::NOISEWARP:
            post.createPass<itg::NoiseWarpPass>();
            break;
        case PostPass::PIXELATE:
            post.createPass<itg::PixelatePass>();
            break;
        case PostPass::LUT:
            post.createPass<itg::LUTPass>();
            break;
        case PostPass::CONTRAST:
            post.createPass<itg::ContrastPass>();
            break;
        case PostPass::SSAO:
            post.createPass<itg::SSAOPass>();
            break;
        case PostPass::HTILTSHIFT:
            post.createPass<itg::HorizontalTiltShifPass>();
            break;
        case PostPass::VTILTSHIFT:
            post.createPass<itg::VerticalTiltShifPass>();
            break;
        case PostPass::RGBSHIFT:
            post.createPass<itg::RGBShiftPass>();
            break;
        case PostPass::FAKESSS:
            post.createPass<itg::FakeSSSPass>();
            break;
        case PostPass::ZOOMBLUR:
            post.createPass<itg::ZoomBlurPass>();
            break;
        case PostPass::BLEACHBYPASS:
            post.createPass<itg::BleachBypassPass>();
            break;
        case PostPass::TOON:
            post.createPass<itg::ToonPass>();
            break;
        case PostPass::GODRAYS:
            post.createPass<itg::GodRaysPass>();
            break;
        case PostPass::RIMHIGHLIGHTING:
            post.createPass<itg::RimHighlightingPass>();
            break;
        case PostPass::LIMBDARKENING:
            post.createPass<itg::LimbDarkeningPass>();
            break;
//        case PostPass::INVERT:
//            post.createPass<itg::Invert>();
//            break;
//        case PostPass::GLITCH:
//            post.createPass<itg::Glitch>();
//            break;
//        case PostPass::ROTATE:
//            post.createPass<itg::Rotate>();
//            break;
//        case PostPass::PIXELSORT:
//            post.createPass<itg::Pixelsort>();
//            break;
//        case PostPass::BEYOON:
//            post.createPass<itg::Beyoon>();
//            break;
//        case PostPass::REFLECTX:
//            post.createPass<itg::ReflectX>();
//            break;
//        case PostPass::REFLECTY:
//            post.createPass<itg::ReflectY>();
//            break;
//        case PostPass::SPLIT:
//            post.createPass<itg::Split>();
//            break;
    }
}

void VOSC::mousePressed(int x, int y, int button) {
    if (showDebug) {
        inspector.mousePressed(x, y, button);
    }
}

void VOSC::mouseReleased(int x, int y, int button) {
    if (showDebug) {
        inspector.mouseReleased(x, y, button);
    }
}

void VOSC::keyPressed(int key) {
    switch (key) {
        case 'f':
            ofToggleFullscreen();
            break;
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
        case 'd':
            showDebug = !showDebug;
            break;
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
        case 'w': {
            for (int i=0; i<layers.size(); i++) {
                if (layers[i]->hasGeom()) {
                    layers[i]->geom->drawWireframe = !layers[i]->geom->drawWireframe;
                }
            }
            break;
        }
        default:
            break;
    }
}

void VOSC::windowResized(int w, int h) {
    if (w > 0 && h > 0) {
        post.init(ofGetWidth(), ofGetHeight());
    }
    layoutLayers(layout);
}

void VOSC::exit() {
    HPV::DestroyHPVEngine();
}
