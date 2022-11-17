#include "ofApp.h"
#include "ofxHPVPlayer.h"
#include "TexturePool.h"
#include "GeomPool.h"
#include "ShaderTex.h"
#include "Args.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

    receiver.setup(33333);
	tidal = new ofxTidalCycles(1);
    setupSounds(INITIAL_SOUNDS);
    setupLayers(INITIAL_VISUALS);
    windowResized(ofGetWidth(), ofGetHeight());
    
    ofDisableArbTex();
}

void ofApp::setupSounds(int numInsts) {
    sounds.resize(numInsts);
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
    parseMessages();
    for (int i=0; i<sounds.size(); i++) {
        sounds[i].update();
    }
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
    Args::update();
    TexturePool::update(sounds, tidal->notes);
    GeomPool::update();
	for (int i = 0; i < layers.size(); i++) {
		layers[i]->update(sounds, tidal->notes);
	}
    if (cam != NULL) {
        ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
        if (easyCam == NULL || !easyCam->getMouseInputEnabled()) {
            cam->setPosition(camPos);
            cam->lookAt(camLook);
        }
        if (camOrbitPerSecond != 0 && easyCam != NULL) {
            camOrbit += ofGetLastFrameTime() * camOrbitPerSecond;
            easyCam->orbitDeg(camOrbit, 0., easyCam->getDistance(), {0., 0., 0.});
        }
    }
    HPV::Update();
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
    if (command.substr(0, 6) == "/sound") {
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
        setupSounds(m.getArgAsInt(0));
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

auto parseIndex(const ofxOscMessage &m) {
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
                ofLog() << "invalid layer index " << which;
            }
        }
    }
    else {
        idx = m.getArgAsInt(0);
    }
    struct retVals { bool all; int idx; };
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
            ofLog() << command + " failed: camera not enabled (run /cam)";
        }
        else if (command == "/cam/pos") {
            Args::getInstance().handleVec3(&camPos, m, 0);
            if (dynamic_cast<ofEasyCam*>(cam) != NULL) {
                cam->setPosition(camPos);
            }
        }
        else if (command == "/cam/look") {
            Args::getInstance().handleVec3(&camLook, m, 0);
            if (dynamic_cast<ofEasyCam*>(cam) != NULL) {
                cam->lookAt(camLook);
            }
        }
        else if (command == "/cam/orbit") {
            if (dynamic_cast<ofEasyCam*>(cam) != NULL) {
                Args::getInstance().handleFloat(&camOrbitPerSecond, m, 0);
            }
            else {
                ofLog() << "/cam/orbit supported only for 'easy' cam";
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
        Args::getInstance().handlePercent(&layer->bri, m);
    }
    else if (command == "/layer/alpha") {
        Args::getInstance().handlePercent(&layer->alpha, m);
    }
    else if (command == "/layer/color") {
        layer->data.setColor(m);
    }
    else if (command == "/layer/tint") {
        layer->data.setTint(m);
    }
    else if (command == "/layer/reset") {
        layer->reset();
    }
    else if (command == "/layer/pos") {
        Args::getInstance().handleVec3(&layer->pos, m);
    }
    else if (command == "/layer/size") {
        Args::getInstance().handleVec3(&layer->data.size, m);
    }
    else if (command == "/layer/rot") {
        Args::getInstance().handleVec3(&layer->rotation, m);
    }
    else if (command == "/layer/rot/speed") {
        Args::getInstance().handleVec3(&layer->rotationSpeed, m);
    }
    else if (command == "/layer/rot/point") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            string argH = m.getArgAsString(1);
            string argV = m.getNumArgs() > 2 ? m.getArgAsString(2) : argH;
            if (argH == "center") {
                layer->rotationPoint.x = 0.5f;
            }
            else if (argH == "left") {
                layer->rotationPoint.x = 0.f;
            }
            else if (argH == "right") {
                layer->rotationPoint.x = 1.f;
            }
            if (argV == "center") {
                layer->rotationPoint.y = 0.5f;
            }
            else if (argV == "top") {
                layer->rotationPoint.y = 0.f;
            }
            else if (argV == "bottom") {
                layer->rotationPoint.y = 1.f;
            }
        }
        else {
            Args::getInstance().handleVec3(&layer->rotationPoint, m);
        }
    }
    else if (command == "/layer/scale") {
        Args::getInstance().handleVec3(&layer->scale, m);
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
    else if (command == "/layer/var") {
        layer->setVar(m);
    }
    else if (command == "/layer/delay") {
        layer->delay = m.getArgAsFloat(1);
    }
    else if (command == "/layer/behaviour") {
        layer->behaviour = m.getArgAsInt(1);
    }
    else if (command == "/layer/visible") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            layer->visibleThresh = {m.getArgAsString(1), m.getArgAsFloat(2), m.getNumArgs() > 3 ? m.getArgAsFloat(3) : 1.f};
        }
        else {
            layer->visibleThresh = {"const", m.getArgAsFloat(1), m.getNumArgs() > 2 ? m.getArgAsFloat(2) : 1.f};
        }
    }
    else if (command == "/layer/onset") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            layer->onsetThresh = {m.getArgAsString(1), m.getArgAsFloat(2), m.getNumArgs() > 3 ? m.getArgAsFloat(3) : 1.f};
        }
        else {
            layer->onsetThresh = {"const", m.getArgAsFloat(1), m.getNumArgs() > 2 ? m.getArgAsFloat(2) : 1.f};
        }
    }
}

void ofApp::indexCommand(Layer *layer, string command, const ofxOscMessage &m) {
    if (command.substr(0, 4) == "/tex") {
        if (command == "/tex" || command == "/tex/choose") {
            Texture* tex;
            if (command == "/tex") {
                string source = m.getArgAsString(1);
                tex = TexturePool::getForShader(source, layer->shader.getId());
            }
            else {
                tex = Texture::choose(m, TexturePool::getShaderPool(layer->shader.getId()));
            }
            layer->shader.setDefaultTexture(tex);
            if (tex->data.size.x == 0 && tex->data.size.y == 0) {
                tex->data.setSize(layer->data.size.x, layer->data.size.y);
            }
            TexturePool::getShaderPool(layer->shader.getId());
            // no need to re-load shared texture
            if (tex->isLoaded()) {
                return;
            }
        }
        textureCommand(layer->shader.getDefaultTexture(), command, m);
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
}

void ofApp::allLayersCommand(string command, const ofxOscMessage &m) {
    for (int i=0; i<layers.size(); i++) {
        indexCommand(layers[i], command, m);
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
            setupSounds(idx+1);
        }
        Sound& sound = sounds[idx];
        soundCommand(sound, command, m);
    }
    else {
        ofLog() << "invalid sound index";
    }
}

void ofApp::soundCommand(Sound& sound, string command, const ofxOscMessage &m) {
    if (command == "/sound/data") {
        sound.parse(m);
        if (waitOnset == -1) {
            waitOnset = 1;
        }
    }
    else if (command == "/sound/stream") {
        sound.stream(m);
    }
    else if (command == "/sound/set") {
        string prop = m.getArgAsString(1);
        if (prop == "maxVol") {
            Args::getInstance().handleFloat(&sound.maxVol, m, 2);
        }
        else if (prop == "maxAmp") {
            Args::getInstance().handleFloat(&sound.maxAmp, m, 2);
        }
        else if (prop == "maxLoud") {
            Args::getInstance().handleFloat(&sound.maxLoud, m, 2);
        }
    }
}

void ofApp::textureCommand(Texture* tex, string command, const ofxOscMessage &m) {
    if (command == "/tex") {
        tex->load(m);
    }
    else if (command == "/tex/choose") {
        tex->tex = Texture::chooseTex(m);
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
    else if (command == "/tex/uniform") {
        if (ShaderTex* shaderTex = dynamic_cast<ShaderTex*>(tex)) {
            shaderTex->setUniform(m);
        }
        else {
            ofLog() << "cannot set uniform for non-shader texture";
        }
    }
    else if (command == "/tex/var") {
        tex->setVar(m);
    }
    else if (command == "/tex/looper") {
        tex->setLooper(m);
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
            Args::getInstance().handlePercent(&data.timePct, m);
        }
    }
    else if (command == "/tex/color") {
        data.setColor(m);
    }
    else if (command == "/tex/tint") {
        data.setTint(m);
    }
    else if (command == "/tex/speed") {
        Args::getInstance().handleFloat(&data.speed, m);
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
    else if (command == "/shader/uniform") {
        shader.setUniform(m);
    }
    else if (command == "/shader/texture") {
        shader.setTexture(m);
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
        Args::getInstance().handleColor(&matSettings.diffuse, m);
    }
    else if (command == "/mat/ambient") {
        Args::getInstance().handleColor(&matSettings.ambient, m);
    }
    else if (command == "/mat/specular") {
        Args::getInstance().handleColor(&matSettings.specular, m);
    }
    else if (command == "/mat/emissive") {
        Args::getInstance().handleColor(&matSettings.emissive, m);
    }
    else if (command == "/mat/shininess") {
        Args::getInstance().handleFloat(&matSettings.shininess, m);
    }
}

void ofApp::processQueue() {
    while (messageQueue.size()) {
        ofxOscMessage &m = messageQueue[0];
        string command = m.getAddress();
        if (command.substr(0, 5) == "/post") {
            post.getPasses().clear();
            for (int i=0; i<m.getNumArgs(); i++) {
                string passName = m.getArgAsString(i);
                createPostPass(passName);
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
                    Texture* tex = TexturePool::getShared(which, true);
                    textureCommand(tex, command, m);
                    if (tex->data.size.x == 0 && tex->data.size.y == 0) {
                        tex->data.setSize(ofGetScreenWidth(), ofGetScreenHeight());
                    }
                }
                else if (command.substr(0, 5) == "/geom") {
                    geomCommand(GeomPool::getShared(which, true), command, m);
                }
            }
            else {
                int idx = m.getArgAsInt(0);
                if (idx > -1 && layers.size() > idx) {
                    indexCommand(layers[idx], command, m);
                }
            }
        }
        messageQueue.erase(messageQueue.begin());
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
    else if (passName == "invert") {
        post.createPass<itg::Invert>();
    }
    else if (passName == "glitch") {
        post.createPass<itg::Glitch>();
    }
    else if (passName == "rotate") {
        post.createPass<itg::Rotate>();
    }
    else if (passName == "pixelsort") {
        post.createPass<itg::Pixelsort>();
    }
    else if (passName == "beyoon") {
        post.createPass<itg::Beyoon>();
    }
    else if (passName == "reflectx") {
        post.createPass<itg::ReflectX>();
    }
    else if (passName == "reflecty") {
        post.createPass<itg::ReflectY>();
    }
    else if (passName == "split") {
        post.createPass<itg::Split>();
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
        if (layers[i]->data.visible) {
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
    
    drawDebug();
}

void ofApp::drawDebug() {
    if (showDebug) {
        ofPushStyle();
        if (sounds.size() > 0) {
            for (int i=0; i<sounds.size(); i++) {
                ofPushMatrix();
                ofTranslate((i+1)*20 + i*200, 20);
                drawVolume(sounds[i]);
                ofPopMatrix();
            }
        }
        // draw amplitude
        // draw mfcc
        for (int i=0; i<layers.size(); i++) {
            if (layers[i]->hasGeom()) {
                ofSetColor(255);
                ofPushMatrix();
                ofTranslate(20+i*120+60, ofGetHeight()-180);
                layers[i]->geom->getMesh().draw(OF_MESH_WIREFRAME);
                ofPopMatrix();
            }
            if (layers[i]->shader.hasDefaultTexture()) {
                ofSetColor(255);
                layers[i]->shader.getDefaultTexture()->getTexture().draw(20+i*120, ofGetHeight()-120, 100, 100);
            }
            else {
                ofFill();
                ofSetColor(0);
                ofDrawRectangle(20+i*120, ofGetHeight()-120, 100, 100);
                ofSetColor(255);
                ofDrawBitmapString("not loaded", 20+i*120, ofGetHeight()-120);
            }
        }
        if (cam != NULL) {
            ofDrawBitmapString(ofToString(cam->getPosition()), 20, 20);
        }
        ofDrawBitmapString(ofToString(ofGetFrameRate()), ofGetWidth()-100, 20);
        ofPopStyle();
    }
}

void ofApp::drawVolume(Sound& sound) {
    ofNoFill();
    //ofSetColor(225);
    ofDrawBitmapString("avg vol (0-100): " + ofToString(sound.volumeScaled * 100.0, 0), 4, 18);
    ofDrawRectangle(0, 0, 200, 200);
    ofFill();
    ofDrawCircle(100, 100, sound.volumeScaled * 140.0f);
    ofBeginShape();
    ofSetColor(245, 58, 135);
    for (unsigned int i = 0; i < sound.volumeHist.size(); i++){
        float x = (float)i/2.f;
        if( i == 0 ) ofVertex(x, 200);
        ofVertex(x, 200 - sound.volumeHist[i] * 70.f);
        if( i == sound.volumeHist.size() -1 ) ofVertex(x, 200);
    }
    ofEndShape(false);
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
        case '0': {
            ofxOscMessage m;
            m.addIntArg(OF_BLENDMODE_DISABLED);
            allLayersCommand("/blendmode", m);
            break;
        }
        case '1':
        case '2':
        case '3':
        case '4':
        case '5': {
            ofxOscMessage m;
            m.addIntArg(static_cast<ofBlendMode>(key - '1' + 1));
            allLayersCommand("/blendmode", m);
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
    post.init(ofGetWidth(), ofGetHeight());
    layoutLayers(layout, layers);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
