#include "ofApp.h"
#include "ColorUtil.h"
#include "ofxHPVPlayer.h"
#include "ShaderTex.h"
#include "TexturePool.h"
#include "GeomPool.h"

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
        layers[i]->setup(i, sounds.size() > i ? "loud" + ofToString(i) : "");
    }
}

void ofApp::layoutLayers(Layout layout, const vector<Layer*> &layers) {
    this->layout = layout;
    for (int i=0; i<layers.size(); i++) {
        layers[i]->layout(layout, i, layers.size());
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    parseMessages();
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
    updateFloats();
    updateVecs();
    updateColors();
    TexturePool::update();
	for (int i = 0; i < layers.size(); i++) {
		layers[i]->update(sounds, tidal->notes);
	}
    if (cam != NULL) {
        cam->setPosition(camPos);
        cam->lookAt(glm::vec3(0));
    }
    HPV::Update();
}

void ofApp::updateFloats() {
    vector<float*> toDelete;
    for (auto const& [key, tween] : floatTweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
        }
        else {
            *key = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from, tween.to, tween.ease);
        }
    }
    for (int i=0; i<toDelete.size(); i++) {
        floatTweens.erase(toDelete[i]);
    }
}

void ofApp::updateVecs() {
    vector<glm::vec3*> toDelete;
    for (auto const& [key, tween] : vec3Tweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
        }
        else {
            glm::vec3 value;
            value.x = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from.x, tween.to.x, tween.ease);
            value.y = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from.y, tween.to.y, tween.ease);
            value.z = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from.z, tween.to.z, tween.ease);
            *key = value;
        }
    }
    for (int i=0; i<toDelete.size(); i++) {
        vec3Tweens.erase(toDelete[i]);
    }
}

void ofApp::updateColors() {
    vector<ofFloatColor*> toDelete;
    for (auto const& [key, tween] : colorTweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
        }
        else {
            float perc = (ofGetElapsedTimef() - tween.start) / tween.dur;
            *key = ofxColorTheory::ColorUtil::lerpLch(tween.from, tween.to, perc);
        }
    }
    for (int i=0; i<toDelete.size(); i++) {
        colorTweens.erase(toDelete[i]);
    }
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
    if (command == "/sound/data") {
        int instNum = m.getArgAsInt(0);
        if ((instNum+1) > sounds.size()) {
            setupSounds(instNum+1);
        }
        sounds[instNum].parse(m);
        if (waitOnset == -1) {
            waitOnset = 1;
        }
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
    else if (command == "/layers") {
        setupLayers(m.getArgAsInt(0));
        if (m.getNumArgs() > 1) {
            layoutLayers(parseLayout(m, 1), layers);
        }
    }
    else if (command == "/cam") {
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
    else if (command == "/cam/pos") {
        handleVec3(&camPos, m, 0);
    }
    else if (command == "/cam/set") {
        string method = m.getArgAsString(0);
        if (method == "nearClip") {
            cam->setNearClip(m.getArgAsFloat(1));
        }
        else if (method == "farClip") {
            cam->setFarClip(m.getArgAsFloat(1));
        }
        else if (method == "autoDistance") {
            dynamic_cast<ofEasyCam*>(cam)->setAutoDistance(m.getArgAsBool(1));
        }
        else if (method == "globalPosition") {
            cam->setGlobalPosition(m.getArgAsFloat(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
        }
        else if (method == "movementMaxSpeed") {
            //dynamic_cast<ofxFirstPersonCamera*>(cam)->setMovementMaxSpeed(m.getArgAsFloat(1));
        }
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

ofFloatColor parseColor(const ofxOscMessage &m, int idx = 0) {
    ofFloatColor color;
    for (int i=idx; i<min(idx+3, (int) m.getNumArgs()); i++) {
        if (m.getArgType(i) == OFXOSC_TYPE_FLOAT) {
            color[i-idx] = m.getArgAsFloat(i);
        }
        else {
            color[i-idx] = m.getArgAsInt(i) / 255.f;
        }
    }
    return color;
}

void ofApp::layerCommand(Layer *layer, string command, const ofxOscMessage &m) {
    if (command.substr(0, 4) == "/tex") {
        if (command == "/tex") {
            string source = m.getArgAsString(1);
            Texture* tex = &TexturePool::getForShader(source, layer->shader.getId());
            layer->shader.setDefaultTexture(tex);
            if (tex->data.size.x == 0 && tex->data.size.y == 0) {
                tex->data.setSize(layer->size.x, layer->size.y);
            }
            if (tex->isLoaded()) {
                return;
            }
        }
        textureCommand(layer->shader.getDefaultTexture(), command, m);
    }
    else if (command.substr(0, 5) == "/geom") {
        if (command == "/geom") {
            string source = m.getArgAsString(1);
            Geom* geom = &GeomPool::getForLayer(source, layer->getId());
            layer->setGeom(geom);
            if (geom->isLoaded()) {
                return;
            }
        }
        geomCommand(layer->geom, command, m);
    }
    else if (command.substr(0, 7) == "/shader") {
        shaderCommand(layer->shader, command, m);
    }
    else if (command.substr(0, 4) == "/mat") {
        materialCommand(layer->matSettings, command, m);
    }
    else if (command == "/bri") {
        handlePercent(&layer->bri, m);
    }
    else if (command == "/alpha") {
        handlePercent(&layer->alpha, m);
    }
    else if (command == "/reset") {
        layer->reset();
    }
    else if (command == "/pos") {
        handleVec3(&layer->pos, m);
    }
    else if (command == "/size") {
        handleVec3(&layer->size, m);
    }
    else if (command == "/rot") {
        handleVec3(&layer->rotation, m);
    }
    else if (command == "/rot/speed") {
        handleVec3(&layer->rotationSpeed, m);
    }
    else if (command == "/rot/point") {
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
            handleVec3(&layer->rotationPoint, m);
        }
    }
    else if (command == "/scale") {
        handleVec3(&layer->scale, m);
    }
    else if (command == "/align") {
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
    else if (command == "/data") {
        vector<string> ds;
        for (int i=1; i<m.getNumArgs(); i++) {
            ds.push_back(m.getArgAsString(i));
        }
        layer->setDataSources(ds);
    }
    else if (command == "/data/add") {
        vector<string> ds;
        for (int i=1; i<m.getNumArgs(); i++) {
            ds.push_back(m.getArgAsString(i));
        }
        layer->addDataSources(ds);
    }
    else if (command == "/looper") {
        layer->shader.getDefaultTexture()->setLooper(m);
    }
    else if (command == "/delay") {
        layer->delay = m.getArgAsFloat(1);
    }
    else if (command == "/behaviour") {
        layer->behaviour = m.getArgAsInt(1);
    }
    else if (command == "/thresh") {
        handleFloat(&layer->thresh, m);
    }
    else if (command == "/thresh/onset" || command == "/onset/thresh") {
        handleFloat(&layer->onsetThresh, m);
    }
    else if (command == "/blendmode") {
        layer->blendMode = static_cast<ofBlendMode>(m.getArgAsInt(0));
    }
}

void ofApp::allLayersCommand(string command, const ofxOscMessage &m) {
    for (int i=0; i<layers.size(); i++) {
        layerCommand(layers[i], command, m);
    }
}

void ofApp::soundCommand(Sound &sound, string command, const ofxOscMessage &m) {
    if (command == "/amp/max") {
        handleFloat(&sound.maxAmp, m);
    }
    else if (command == "/loud/max") {
        handleFloat(&sound.maxLoud, m);
    }

}

void ofApp::textureCommand(Texture* tex, string command, const ofxOscMessage &m) {
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
    else if (command == "/tex/frames") {
        tex->setNumFrames(m.getArgAsInt(1));
    }
    else if (command == "/tex/shader/uniform") {
        if (ShaderTex* shaderTex = dynamic_cast<ShaderTex*>(tex->tex)) {
            shaderTex->setUniform(m);
        }
    }
    else {
        texDataCommand(tex->data, command, m);
    }
}

void ofApp::texDataCommand(TexData& data, string command, const ofxOscMessage &m) {
    if (command == "/tex/size") {
        data.setSize(m);
    }
    else if (command == "/tex/noclear") {
        data.noClear = m.getArgAsBool(1);
    }
    else if (command == "/tex/blendmode") {
        data.blendMode = static_cast<ofBlendMode>(m.getArgAsInt(1));
    }
    else if (command == "/tex/aspectratio") {
        data.aspectRatio = m.getArgAsBool(1);
    }
    else if (command == "/tex/seek") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            if (m.getArgAsString(1) == "rand") {
                data.timePct = ofRandom(1.f);
            }
        }
        else {
            handlePercent(&data.timePct, m);
        }
    }
    else if (command == "/tex/color") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            string value = m.getArgAsString(1);
            if (value == "rand") {
                data.useRandomColor = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
                if (data.useRandomColor) {
                    data.useMFCCColor = false;
                }
            }
            else if (value == "mfcc") {
                data.useMFCCColor = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
                if (data.useMFCCColor) {
                    data.useRandomColor = false;
                }
            }
            else if (value == "lerp") {
                ofFloatColor fromColor = parseColor(m, 3);
                ofFloatColor toColor = parseColor(m, 6);
                float perc = m.getArgAsFloat(2);
                data.color = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
                data.useMFCCColor = false;
                data.useRandomColor = false;
            }
        }
        else {
            handleColor(&data.color, m);
            data.useMFCCColor = false;
            data.useRandomColor = false;
        }
    }
    else if (command == "/tex/tint") {
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            string value = m.getArgAsString(1);
            if (value == "rand") {
                data.useRandomTint = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
                if (data.useRandomTint) {
                    data.useMFCCTint = false;
                }
            }
            else if (value == "mfcc") {
                data.useMFCCTint = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
                if (data.useMFCCTint) {
                    data.useRandomTint = false;
                }
            }
            else if (value == "lerp") {
                ofFloatColor fromColor = parseColor(m, 3);
                ofFloatColor toColor = parseColor(m, 6);
                float perc = m.getArgAsFloat(2);
                data.tint = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
                data.useMFCCTint = false;
                data.useRandomTint = false;
            }
        }
        else {
            handleColor(&data.tint, m);
            data.useMFCCTint = false;
            data.useRandomTint = false;
        }
    }
    else if (command == "/tex/speed") {
        handleFloat(&data.speed, m);
    }
    else if (command == "/tex/fbo") {
        data.setFboSettings(m);
    }
}

void ofApp::shaderCommand(Shader& shader, string command, const ofxOscMessage& m) {
    if (command == "/shader") {
        shader.load(m.getArgAsString(1));
    }
    else if (command == "/shader/uniform") {
        shader.setUniform(m);
    }
    else if (command == "/shader/texture") {
        shader.setTexture(m);
    }
    else if (command == "/shader/geom/intype") {
        shader.getShader()->setGeometryInputType(static_cast<GLenum>(m.getArgAsInt(1)));
    }
    else if (command == "/shader/geom/outtype") {
        shader.getShader()->setGeometryOutputType(static_cast<GLenum>(m.getArgAsInt(1)));
    }
    else if (command == "/shader/geom/outcount") {
        shader.getShader()->setGeometryOutputCount(m.getArgAsInt(1));
    }
}

void ofApp::geomCommand(Geom* geom, string command, const ofxOscMessage& m) {
    if (command == "/geom") {
        geom->load(m);
    }
    else if (command == "/geom/instanced") {
        geom->drawInstanced = m.getArgAsInt(1);
    }
    else if (command == "/geom/mesh/mode") {
        geom->getMesh().setMode(static_cast<ofPrimitiveMode>(m.getArgAsInt(1)));
    }
}

void ofApp::materialCommand(ofMaterialSettings& matSettings, string command, const ofxOscMessage& m) {
    if (command == "/mat/diffuse") {
        handleColor(&matSettings.diffuse, m);
    }
    else if (command == "/mat/ambient") {
        handleColor(&matSettings.ambient, m);
    }
    else if (command == "/mat/specular") {
        handleColor(&matSettings.specular, m);
    }
    else if (command == "/mat/emissive") {
        handleColor(&matSettings.emissive, m);
    }
    else if (command == "/mat/shininess") {
        handleFloat(&matSettings.shininess, m);
    }
}

void ofApp::handleFloat(float *value, const ofxOscMessage &m) {
    if (m.getNumArgs() > 2) {
        if (m.getNumArgs() > 3) {
            createTween(value, m.getArgAsFloat(1), m.getArgAsFloat(2), m.getArgAsString(3));
        }
        else {
            createTween(value, m.getArgAsFloat(1), m.getArgAsFloat(2));
        }
    }
    else {
        *value = m.getArgAsFloat(1);
    }
}

void ofApp::handlePercent(float *value, const ofxOscMessage &m) {
    if (m.getNumArgs() > 2) {
        if (m.getNumArgs() > 3) {
            createTween(value,
                        m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1),
                        m.getArgType(2) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(2) > 1.f ? m.getArgAsFloat(2) / 100.f : m.getArgAsFloat(2),
                        m.getArgAsString(3));
        }
        else {
            createTween(value,
                        m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1),
                        m.getArgType(2) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(2) > 1.f ? m.getArgAsFloat(2) / 100.f : m.getArgAsFloat(2));
        }
    }
    else {
        *value = m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1);
    }
}

void ofApp::handleVec3(glm::vec3 *value, const ofxOscMessage &m, int firstArg) {
    if (m.getNumArgs() > firstArg + 3) {
        createTween(value, glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getArgAsFloat(firstArg+2)), m.getArgAsFloat(firstArg+3));
    }
    else if (m.getNumArgs() > firstArg+1) {
        *value = glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getNumArgs() > firstArg+2 ? m.getArgAsFloat(firstArg+2) : 0);
    }
    else {
        *value = glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg));
    }
}

void ofApp::handleColor(ofFloatColor *value, const ofxOscMessage &m) {
    if (m.getNumArgs() > 4) {
        createTween(value, parseColor(m, 1), m.getArgAsFloat(4));
    }
    else {
        *value = parseColor(m, 1);
    }
}

void ofApp::createTween(float *value, float target, float dur, ofxeasing::function ease) {
    Tween<float> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    floatTweens[value] = tween;
}

void ofApp::createTween(glm::vec3 *value, const glm::vec3 &target, float dur, ofxeasing::function ease) {
    Tween<glm::vec3> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    vec3Tweens[value] = tween;
}

void ofApp::createTween(ofFloatColor *value, const ofFloatColor &target, float dur, ofxeasing::function ease) {
    Tween<ofFloatColor> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    colorTweens[value] = tween;
}

void ofApp::processQueue() {
    while (messageQueue.size()) {
        ofxOscMessage &m = messageQueue[0];
        string command = m.getAddress();
        if (command == "/layout") {
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
        else if (command.substr(0, 4) == "/amp" || command.substr(0, 5) == "/loud") {
            auto [all, idx] = parseIndex(m);
            if (all) {
                for (int i=0; i<sounds.size(); i++) {
                    soundCommand(sounds[i], command, m);
                }
            }
            else {
                if (idx > -1) {
                    soundCommand(sounds[idx], command, m);
                }
            }
        }
        else if (command.substr(0, 5) == "/post") {
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
                    textureCommand(&TexturePool::getShared(which, true), command, m);
                }
                else if (command.substr(0, 5) == "/geom") {
                    geomCommand(&GeomPool::getShared(which, true), command, m);
                }
            }
            else {
                int idx = m.getArgAsInt(0);
                if (idx > -1) {
                    layerCommand(layers[idx], command, m);
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
    ofDisableDepthTest();
    ofPopMatrix();
    
    if (showDebug) {
        ofPushStyle();
        for (int i=0; i<layers.size(); i++) {
            ofSetColor(255);
            ofPushMatrix();
            ofTranslate(20+i*120+60, ofGetHeight()-180);
            layers[i]->geom->getMesh().draw(OF_MESH_WIREFRAME);
            ofPopMatrix();
            if (layers[i]->shader.isLoaded()) {
                ofSetColor(255);
                layers[i]->shader.getDefaultTexture()->getTexture().draw(20+i*120, ofGetHeight()-120, 100, 100);
                if (layers[i]->data.values.size() > 0) {
                    ofFill();
                    ofDrawRectangle(20+i*120, ofGetHeight()-120, layers[i]->data.values[0]*100.f, 10);
                }
            }
            else {
                ofFill();
                ofSetColor(0);
                ofDrawRectangle(20+i*120, ofGetHeight()-120, 100, 100);
                ofSetColor(255);
                ofDrawBitmapString("not loaded", 20+i*120, ofGetHeight()-120);
            }
        }
        ofDrawBitmapString(ofToString(ofGetFrameRate()), ofGetWidth()-100, 20);
        ofPopStyle();
    }
    ofDisableBlendMode();
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
            allLayersCommand("/reload", m);
            break;
        }
        case 'w': {
            for (int i=0; i<layers.size(); i++) {
                layers[i]->geom->drawWireframe = !layers[i]->geom->drawWireframe;
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
