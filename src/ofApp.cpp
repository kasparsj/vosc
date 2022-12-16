#include "ofApp.h"
#include "ofxHPVPlayer.h"
#include "TexturePool.h"
#include "GeomPool.h"
#include "VariablePool.h"
#include "ShaderTex.h"
#include "Args.h"

#ifndef TARGET_CPU_UNIVERSAL
#include "ofxUltralight.h"
#endif

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

    receiver.setup(33333);
	tidal = new ofxTidalCycles(1);
    setupLayers(INITIAL_VISUALS);
    gui.setup();
    windowResized(ofGetWidth(), ofGetHeight());
    
    ofDisableArbTex();
    
    camPos = VariablePool::getOrCreateShared<glm::vec3>("camPos");
    camPos->set(glm::vec3(0, 0, -870));
    
    camLook = VariablePool::getOrCreateShared<glm::vec3>("camLook");
    camLook->set(glm::vec3(0));
    
    console = std::make_shared<Logger>();
    ofSetLoggerChannel(console);
    
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

void ofApp::setupLayers(int numVisuals) {
    layers.resize(numVisuals);
    for (int i=0; i<layers.size(); i++) {
        if (layers[i] == NULL) {
            layers[i] = new Layer();
        }
        layers[i]->setup(i);
    }
}

void ofApp::layoutLayers(Layout layout, const vector<Layer*> &layers) {
    this->layout = layout;
    for (int i=0; i<layers.size(); i++) {
        // todo: add support for animation
        layers[i]->layout(layout, i, layers.size());
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
    if (easyCam != NULL) {
        camPos->set(cam->getPosition());
        camLook->set(cam->getLookAtDir());
    }
    parseMessages();
    for (int i=0; i<mics.size(); i++) {
        mics[i].update();
    }
    for (int i=0; i<sounds.size(); i++) {
        sounds[i].update();
    }
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
    Args::update();
    VariablePool::update(mics, sounds, tidal->notes);
    TexturePool::update(sounds, tidal->notes);
    GeomPool::update();
	for (int i = 0; i < layers.size(); i++) {
		layers[i]->update(mics, sounds, tidal->notes);
	}
    if (cam != NULL) {
        cam->setPosition(camPos->get());
        cam->lookAt(camLook->get());
        if (camOrbitPerSecond != 0 && easyCam != NULL) {
            camOrbit += ofGetLastFrameTime() * camOrbitPerSecond;
            easyCam->orbitDeg(camOrbit, 0., easyCam->getDistance(), {0., 0., 0.});
        }
    }
    HPV::Update();
#ifndef TARGET_CPU_UNIVERSAL
    ofxUltralight::update();
#endif
}

void ofApp::parseMessages(){
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

void ofApp::parseMessage(const ofxOscMessage &m) {
    string command = m.getAddress();
    if (command.substr(0, 4) == "/mic") {
        micsCommand(command, m);
    }
    else if (command.substr(0, 5) == "/midi") {
        midiCommand(command, m);
    }
    else if (command.substr(0, 6) == "/sound") {
        soundsCommand(command, m);
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
    else if (command == "/sounds") {
        sounds.resize(m.getArgAsInt(0));
    }
    else if (command.substr(0, 7) == "/layers") {
        layersCommand(command, m);
    }
    else if (command.substr(0, 4) == "/cam") {
        cameraCommand(command, m);
    }
    else if (command.substr(0, 6) == "/light") {
        lightCommand(command, m);
    }
    else {
        messageQueue.push_back(m);
    }
}

bool ofApp::checkOnset() {
    bool isOnset;
    if (tidal->notes.size()) {
        isOnset = true;
    }
    else {
        for (int i=0; i<sounds.size(); i++) {
            if (sounds[i].onset) {
                isOnset = true;
                break;
            }
        }
    }
    return isOnset;
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

void ofApp::layersCommand(string command, const ofxOscMessage& m) {
    if (command == "/layers") {
        setupLayers(m.getArgAsInt(0));
        if (m.getNumArgs() > 1) {
            layoutLayers(parseLayout(m, 1), layers);
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
        layoutLayers(parseLayout(m, 0), layers);
    }
}

void ofApp::cameraCommand(string command, const ofxOscMessage& m) {
    if (command == "/cam") {
        string name = m.getArgAsString(0);
        if (name == "easy") {
            cam = new ofEasyCam();
        }
        else if (name != "") {
            cam = new ofCamera();
        }
        else {
            delete cam;
            cam = NULL;
        }
    }
    else {
        if (cam == NULL) {
            ofLog() << (command + " failed: camera not enabled (run /cam)");
        }
        else if (command == "/cam/pos") {
//            if (Args::get().isTweenVec3(m, 0)) {
//                ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
//                if (easyCam != NULL) {
//                    easyCam->disableMouseInput();
//                }
//                function<void()> onComplete = [easyCam]() {
//                    easyCam->enableMouseInput();
//                };
//                camPos->tween(m, 0, onComplete);
//            }
//            else {
                camPos->set(m, 0);
//            }
        }
        else if (command == "/cam/look") {
//            if (Args::get().isTweenVec3(m, 0)) {
//                ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
//                if (easyCam != NULL) {
//                    easyCam->disableMouseInput();
//                }
//                function<void()> onComplete = [easyCam]() {
//                    easyCam->enableMouseInput();
//                };
//                camLook->tween(m, 0, onComplete);
//            }
//            else {
                camLook->set(m, 0);
//            }
        }
        else if (command == "/cam/orbit") {
            if (dynamic_cast<ofEasyCam*>(cam) != NULL) {
                Args::get().handleFloat(&camOrbitPerSecond, m, 0);
            }
            else {
                ofLogError() << ("/cam/orbit supported only for 'easy' cam");
            }
        }
        else if (command == "/cam/set") {
            string method = m.getArgAsString(0);
            if (method == "nearClip") {
                cam->setNearClip(m.getArgAsFloat(1));
            }
            else if (method == "farClip") {
                cam->setFarClip(m.getArgAsFloat(1));
            }
            else if (method == "globalPosition") {
                cam->setGlobalPosition(m.getArgAsFloat(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
            }
            else {
                ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
                if (easyCam != NULL) {
                    if (method == "distance") {
                        easyCam->setDistance(m.getArgAsFloat(1));
                    }
                    else if (method == "autoDistance") {
                        dynamic_cast<ofEasyCam*>(cam)->setAutoDistance(m.getArgAsBool(1));
                    }
                    else if (method == "mouseInput") {
                        if (m.getArgAsBool(1)) {
                            dynamic_cast<ofEasyCam*>(cam)->enableMouseInput();
                        }
                        else {
                            dynamic_cast<ofEasyCam*>(cam)->disableMouseInput();
                        }
                    }
                }
    //            ofxFirstPersonCamera* firstPersonCam = dynamic_cast<ofxFirstPersonCamera*>(cam);
    //            if (firstPersonCam != NULL) {
    //                if (method == "movementMaxSpeed") {
    //                    firstPersonCam->setMovementMaxSpeed(m.getArgAsFloat(1));
    //                }
    //            }
            }
        }
    }
}

void ofApp::lightCommand(string command, const ofxOscMessage& m) {
    if (command == "/light") {
        lights.create(m);
    }
    else if (command == "/light/remove") {
        lights.remove(m);
    }
}

void ofApp::layerCommand(Layer* layer, string command, const ofxOscMessage& m) {
    if (command == "/layer/bri") {
        layer->setVar("bri", m);
    }
    else if (command == "/layer/alpha") {
        layer->setVar("alpha", m);
    }
    else if (command == "/layer/tint") {
        layer->setVar("tint", m);
    }
    else if (command == "/layer/reset") {
        layer->reset();
    }
    else if (command == "/layer/pos") {
        layer->setVar("pos", m);
    }
    else if (command == "/layer/size") {
        Args::get().handleVec3(&layer->data.size, m);
    }
    else if (command == "/layer/rot") {
        layer->setVar("rotation", m);
    }
    else if (command == "/layer/pivot") {
        layer->setVar("pivot", m);
    }
    else if (command == "/layer/scale") {
        layer->setVar("scale", m);
    }
    else if (command == "/layer/align") {
        ofAlignHorz alignH;
        ofAlignVert alignV;
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            string argH = m.getArgAsString(1);
            string argV = m.getNumArgs() > 2 ? m.getArgAsString(2) : argH;
            if (argH == "center") {
                alignH = OF_ALIGN_HORZ_CENTER;
            }
            else if (argH == "left") {
                alignH = OF_ALIGN_HORZ_LEFT;
            }
            else if (argH == "right") {
                alignH = OF_ALIGN_HORZ_RIGHT;
            }
            if (argV == "center") {
                alignV = OF_ALIGN_VERT_CENTER;
            }
            else if (argV == "top") {
                alignV = OF_ALIGN_VERT_TOP;
            }
            else if (argV == "bottom") {
                alignV = OF_ALIGN_VERT_BOTTOM;
            }
        }
        else {
            alignH = static_cast<ofAlignHorz>(m.getArgAsInt(1));
            alignV = static_cast<ofAlignVert>(m.getNumArgs() > 2 ? m.getArgAsInt(2) : m.getArgAsInt(1) * 16);
        }
        layer->alignH = alignH;
        layer->alignV = alignV;
    }
    else if (command == "/layer/delay") {
        layer->delay = m.getArgAsFloat(1);
    }
    else if (command == "/layer/behaviour") {
        layer->behaviour = m.getArgAsInt(1);
    }
    else if (command == "/layer/visible") {
        layer->setVar("visible", m);
    }
    else if (command == "/layer/solo") {
        layer->setVar("visible", true);
        for (int i=0; i<layers.size(); i++) {
            if (layer != layers[i]) {
                layers[i]->setVar("visible", false);
            }
        }
    }
}

void ofApp::indexCommand(Layer *layer, string command, const ofxOscMessage &m) {
    if (command.substr(0, 4) == "/tex") {
        shared_ptr<Texture>& tex = layer->shader.getDefaultTexture();
        bool isShared = false;
        if (command == "/tex" || command == "/tex/choose" || tex == NULL) {
            if (command == "/tex") {
                string name = m.getArgAsString(1);
                if (TexturePool::hasShared(name)) {
                    tex = TexturePool::getShared(name);
                    isShared = true;
                }
                else {
                    tex = TexturePool::getForShader(DEFAULT_TEX, layer->shader.getId());
                }
            }
            else {
                tex = TexturePool::getForShader(DEFAULT_TEX, layer->shader.getId());
            }
            layer->shader.setDefaultTexture(tex);
            if (tex->data.size.x == 0 && tex->data.size.y == 0) {
                tex->data.setSize(layer->data.size.x, layer->data.size.y);
            }
        }
        if (isShared) {
            return;
        }
        textureCommand(tex, command, m);
    }
    else if (command.substr(0, 4) == "/var") {
        string name = m.getArgAsString(1);
        layer->setVar(name, m, 2);
    }
    else if (command.substr(0, 5) == "/geom") {
        if (command == "/geom" || command == "/geom/choose") {
            Geom* geom;
            if (command == "/geom") {
                string source = m.getArgAsString(1);
                geom = GeomPool::getForLayer(source, layer->getId());
            }
            else {
                geom = GeomPool::getForLayer(layer->getId());
                geom->choose(m);
            }
            layer->setGeom(geom);
            if (geom->isLoaded()) {
                return;
            }
        }
        geomCommand(layer->geom, command, m);
    }
    else if (command.substr(0, 6) == "/layer") {
        layerCommand(layer, command, m);
    }
    else if (command.substr(0, 7) == "/shader") {
        shaderCommand(layer->shader, command, m);
    }
    else if (command.substr(0, 4) == "/mat") {
        materialCommand(layer->matSettings, command, m);
    }
    else {
        ofLog() << "invalid indexCommand: " << m;
    }
}

void ofApp::allLayersCommand(string command, const ofxOscMessage &m) {
    for (int i=0; i<layers.size(); i++) {
        indexCommand(layers[i], command, m);
    }
}

void ofApp::micsCommand(string command, const ofxOscMessage &m) {
    if (command == "/mic/list") {
        vector<ofSoundDevice> devices = ofSoundStreamListDevices();
        ofLog() << ("ofSoundStreamListDevices:");
        for (int i=0; i<devices.size(); i++) {
            ofLog() << (ofToString(i) + ": " + devices[i].name);
        }
    }
    else {
        auto [all, idx] = parseIndex(m);
        if (all) {
            for (int i=0; i<mics.size(); i++) {
                micCommand(mics[i], command, m);
            }
        }
        else if (idx >= 0) {
            if (!all && (idx+1) > mics.size()) {
                mics.resize(idx+1);
            }
            Mic& mic = mics[idx];
            micCommand(mic, command, m);
        }
        else {
            ofLogError() << ("invalid sound index: " + ofToString(idx));
        }
    }
}

void ofApp::micCommand(Mic& mic, string command, const ofxOscMessage &m) {
    if (command == "/mic") {
        mic.setupStream(m);
    }
    else if (command == "/mic/set") {
        string prop = m.getArgAsString(1);
        if (prop == "maxAmp") {
            Args::get().handleFloat(&mic.maxAmp, m, 2);
        }
    }
}

void ofApp::midiCommand(string command, const ofxOscMessage &m) {
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

void ofApp::soundsCommand(string command, const ofxOscMessage &m) {
    auto [all, idx] = parseIndex(m);
    if (all) {
        for (int i=0; i<sounds.size(); i++) {
            soundCommand(sounds[i], command, m);
        }
    }
    else if (idx >= 0) {
        if (!all && (idx+1) > sounds.size()) {
            sounds.resize(idx+1);
        }
        Sound& sound = sounds[idx];
        soundCommand(sound, command, m);
    }
    else {
        ofLogError() << ("invalid sound index: " + ofToString(idx));
    }
}

void ofApp::soundCommand(Sound& sound, string command, const ofxOscMessage &m) {
    if (command == "/sound/data") {
        sound.parse(m);
        if (waitOnset == -1) {
            waitOnset = 1;
        }
    }
    else if (command == "/sound/set") {
        string prop = m.getArgAsString(1);
        if (prop == "maxAmp") {
            Args::get().handleFloat(&sound.maxAmp, m, 2);
        }
        else if (prop == "maxLoud") {
            Args::get().handleFloat(&sound.maxLoud, m, 2);
        }
    }
}

void ofApp::textureCommand(shared_ptr<Texture>& tex, string command, const ofxOscMessage &m) {
    if (command == "/tex") {
        tex->load(m);
    }
    else if (command == "/tex/choose") {
        tex->choose(m);
    }
    else if (command == "/tex/reload") {
        tex->reload();
    }
    else if (command == "/tex/unload") {
        tex->unload();
    }
    else if (command == "/tex/clear") {
        tex->clear();
    }
    else if (command == "/tex/set") {
        string method = m.getArgAsString(1);
        if (method == "textureWrap") {
            tex->getTexture().setTextureWrap(m.getArgAsInt(2), m.getArgAsInt(3));
        }
        else if (method == "numFrames") {
            tex->setNumFrames(m.getArgAsInt(2));
        }
        else {
            texDataCommand(tex->data, command, m);
        }
    }
    else if (command == "/tex/looper") {
        tex->setLooper(m);
    }
    else if (command == "/tex/color") {
        tex->setVar("color", m);
    }
    // todo: there is no way to multiply tints, therefore texture tint is disabled temporarily
//    else if (command == "/tex/tint") {
//        tex->setVar("tint", m);
//    }
    else if (command == "/tex/speed") {
        tex->setVar("speed", m);
    }
    else if (command.substr(0, 8) == "/tex/var") {
        string name = m.getArgAsString(1);
        tex->setVar(name, m, 2);
    }
    else {
        texDataCommand(tex->data, command, m);
    }
}

void ofApp::texDataCommand(TexData& data, string command, const ofxOscMessage &m) {
    if (command == "/tex/size") {
        data.setSize(m);
    }
    else if (command == "/tex/set") {
        data.set(m);
    }
    else if (command == "/tex/seek") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            if (m.getArgAsString(1) == "rand") {
                data.timePct = ofRandom(1.f);
            }
        }
        else {
            Args::get().handlePercent(&data.timePct, m);
        }
    }
    else if (command == "/tex/fbo") {
        data.setFboSettings(m);
    }
}

void ofApp::shaderCommand(Shader& shader, string command, const ofxOscMessage& m) {
    if (command == "/shader") {
        shader.load(m.getArgAsString(1));
    }
    else if (command == "/shader/set") {
        shader.set(m);
    }
    else if (command == "/shader/var") {
        string name = m.getArgAsString(1);
        shader.setVar(name, m, 2);
    }
    else if (command == "/shader/texture") {
        shader.setTexture(m);
    }
    else if (command == "/shader/buffer") {
        shader.setBuffer(m);
    }
}

void ofApp::geomCommand(Geom* geom, string command, const ofxOscMessage& m) {
    if (command == "/geom") {
        geom->load(m);
    }
    else if (command == "/geom/choose") {
        geom->choose(m);
    }
    else if (command == "/geom/set") {
        geom->set(m);
    }
}

void ofApp::materialCommand(ofMaterialSettings& matSettings, string command, const ofxOscMessage& m) {
    if (command == "/mat/diffuse") {
        Args::get().handleColor(&matSettings.diffuse, m);
    }
    else if (command == "/mat/ambient") {
        Args::get().handleColor(&matSettings.ambient, m);
    }
    else if (command == "/mat/specular") {
        Args::get().handleColor(&matSettings.specular, m);
    }
    else if (command == "/mat/emissive") {
        Args::get().handleColor(&matSettings.emissive, m);
    }
    else if (command == "/mat/shininess") {
        Args::get().handleFloat(&matSettings.shininess, m);
    }
}

void ofApp::processQueue() {
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
                    textureCommand(tex, command, m);
                    if (tex->data.size.x == 0 && tex->data.size.y == 0) {
                        tex->data.setSize(ofGetScreenWidth(), ofGetScreenHeight());
                    }
                }
                else if (command.substr(0, 4) == "/var") {
                    VariablePool::getOrCreateShared(which, m, 1);
                }
                else if (command.substr(0, 5) == "/geom") {
                    geomCommand(GeomPool::getShared(which, true), command, m);
                }
                else {
                    invalidCommand(m);
                }
            }
            else if (m.getArgType(0) == OFXOSC_TYPE_INT32) {
                int idx = m.getArgAsInt(0);
                if (idx > -1 && layers.size() > idx) {
                    indexCommand(layers[idx], command, m);
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

void ofApp::invalidCommand(const ofxOscMessage& m) {
    ofLogError() << "command not recognized: " << m;
    for (int i=0; i<m.getNumArgs(); i++) {
        if (m.getArgType(i) == OFXOSC_TYPE_BLOB) {
            ofLog() << m.getArgAsBlob(i);
        }
    }
}

void ofApp::createPostPass(string passName) {
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

void ofApp::createPostPass(int passId) {
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

//--------------------------------------------------------------
void ofApp::draw(){
    ofPushMatrix();
    if (cam != NULL) {
        ofEnableDepthTest();
        lights.enable();
        post.begin(*cam);
        ofTranslate(-ofGetWidth()/2.f, -ofGetHeight()/2);
    }
    else {
        post.begin();
    }
    ofClear(0, 0, 0, 0);
    int totalVisible = 0;
    for (int i=0; i<layers.size(); i++) {
        if (layers[i]->getVarBool("visible")) {
            totalVisible++;
        }
    }
    for (int i=0; i<layers.size(); i++) {
        layers[i]->draw(totalVisible);
    }
    post.end();
    lights.disable();
    ofDisableDepthTest();
    ofPopMatrix();
    
    if (showDebug) {
        drawDebug();
    }
    drawConsole();
}

void ofApp::drawDebug() {
    if (cam != NULL) {
        ofDrawBitmapString(ofToString(cam->getPosition()), 20, 20);
    }
    ofDrawBitmapString(ofToString(ofGetFrameRate()), ofGetWidth()-100, 20);
    
    ofPushMatrix();
    
    if (debugLayer < 0) {
        drawDebugGlobals();
    }
    else {
        drawDebugLayer(debugLayer);
    }
    
    ofPopMatrix();
}

void ofApp::drawDebugGlobals() {
    ofTranslate(20, 60);
    ofDrawBitmapString("Geometry", 0, -20);
    ofPushStyle();
    debugGeoms();
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Textures", 0, -20);
    ofPushStyle();
    debugLayerTextures();
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Microphones", 0, -20);
    drawMics();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Sounds", 0, -20);
    drawSounds();
}

void ofApp::drawDebugLayer(int i) {
    ofTranslate(20, 60);
    ofDrawBitmapString("Geometry", 0, -20);
    ofPushStyle();
    debugGeom(i);
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Default Texture", 0, -20);
    ofPushStyle();
    if (layers[i]->shader.hasDefaultTexture()) {
        debugTexture(layers[i]->shader.getDefaultTexture()->getTexture());
    }
    else {
        debugEmpty("not loaded");
    }
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Shader Textures", 0, -20);
    ofPushStyle();
    debugShaderTextures(i);
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Shader Buffers", 0, -20);
    ofPushStyle();
    debugShaderBuffers(i);
    ofPopStyle();
}

void ofApp::debugGeoms() {
    for (int i=0; i<layers.size(); i++) {
        debugGeom(i);
    }
}

void ofApp::debugGeom(int i) {
    if (layers[i]->hasGeom()) {
        ofSetColor(255);
        ofPushMatrix();
        ofTranslate(i*120+60, 0);
        layers[i]->geom->getMesh().draw(OF_MESH_WIREFRAME);
        ofPopMatrix();
    }
}

void ofApp::debugLayerTextures() {
    for (int i=0; i<layers.size(); i++) {
        if (layers[i]->shader.hasDefaultTexture()) {
            debugTexture(layers[i]->shader.getDefaultTexture()->getTexture());
        }
        else {
            debugEmpty("not loaded");
        }
        ofTranslate(120, 0);
    }
}

void ofApp::debugShaderTextures(int i) {
    const map<string, shared_ptr<Texture>>& textures = layers[i]->shader.getTextures();
    for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
        debugTexture(it->second->getTexture());
        ofTranslate(120, 0);
    }
}

void ofApp::debugShaderBuffers(int i) {
    const map<string, shared_ptr<Buffer>>& buffers = layers[i]->shader.getBuffers();
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        debugTexture(it->second->getTexture());
        ofTranslate(120, 0);
    }
}

void ofApp::debugTexture(ofTexture& tex) {
    ofSetColor(255);
    tex.draw(0, 0, 100, 100);
}

void ofApp::debugEmpty(string text) {
    ofFill();
    ofSetColor(0);
    ofDrawRectangle(0, 0, 100, 100);
    ofSetColor(255);
    ofDrawBitmapString(text, 8, 20);
}

void ofApp::drawMics() {
    if (mics.size() > 0) {
        for (int i=0; i<mics.size(); i++) {
            ofPushMatrix();
            ofTranslate((i+1)*20 + i*200, 0);
            drawAmplitude(mics[i]);
            ofPopMatrix();
        }
    }
}

void ofApp::drawSounds() {
    if (sounds.size() > 0) {
        for (int i=0; i<sounds.size(); i++) {
            ofPushMatrix();
            ofTranslate((i+1)*20 + i*200, 0);
            drawAmplitude(sounds[i]);
            ofPopMatrix();
        }
    }
    // draw mfcc
}

void ofApp::drawAmplitude(Mic& sound) {
    ofPushStyle();
    ofNoFill();
    ofSetColor(225);
    ofDrawBitmapString(ofToString(sound.ampScaled * 100.0, 0), 4, 18);
    ofDrawRectangle(0, 0, 100, 100);
    ofFill();
    ofDrawCircle(50, 50, sound.ampScaled * 100.0f);
    ofBeginShape();
    ofSetColor(245, 58, 135);
    for (unsigned int i = 0; i < sound.ampHist.size(); i++){
        float x = (float)i/2.f;
        if( i == 0 ) ofVertex(x, 100);
        ofVertex(x, 100 - sound.ampHist[i] * 100.f);
        if( i == sound.ampHist.size() -1 ) ofVertex(x, 100);
    }
    ofEndShape(false);
    ofPopStyle();
}

void ofApp::drawConsole() {
    if (console->hasNewErrors()) {
        showConsole = true;
    }
    if (showDebug || showConsole) {
        gui.begin();
        
        console->draw(&showConsole);

        gui.end();
    }
}

void ofApp::exit() {
    HPV::DestroyHPVEngine();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
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
                if (key == '0') {
                    debugLayer = -1;
                }
                else {
                    debugLayer = key - '1';
                }
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
        case 'c':
            showConsole = !showConsole;
            break;
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

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
    if (w > 0 && h > 0) {
        post.init(ofGetWidth(), ofGetHeight());
    }
    layoutLayers(layout, layers);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
