#include "ofApp.h"
#include "ColorUtil.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

    receiver.setup(33333);
	tidal = new ofxTidalCycles(1);
    setupSounds(MAX_SOUNDS);
    setupLayers(MAX_VISUALS);
    windowResized(ofGetWidth(), ofGetHeight());
}

void ofApp::setupSounds(int numInsts) {
    sounds.resize(numInsts);
}

void ofApp::setupLayers(int numVisuals) {
    layers.resize(numVisuals);
    for (int i=0; i<layers.size(); i++) {
        layers[i].setup(i, numVisuals, sounds.size() > i ? "amp" + ofToString(i) : "");
    }
}

void ofApp::layoutLayers(Layout layout) {
    for (int i=0; i<layers.size(); i++) {
        layers[i].layout(layout);
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    parseMessages();
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
	for (int i = 0; i < layers.size(); i++) {
		layers[i].update(sounds, tidal->notes, config);
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

void ofApp::parseMessage(const ofxOscMessage &m) {
    string command = m.getAddress();
    if (command == "/sound/data") {
        int instNum = m.getArgAsInt(0);
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
            layoutLayers(static_cast<Layout>(m.getArgAsInt(1)));
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

void ofApp::processQueue() {
    while (messageQueue.size()) {
        ofxOscMessage &m = messageQueue[0];
        string command = m.getAddress();
        if (command == "/layout") {
            layoutLayers(static_cast<Layout>(m.getArgAsInt(0)));
        }
        else if (command == "/amp/max") {
            config.maxAmp = m.getArgAsFloat(0);
        }
        else if (command == "/amp/thresh") {
            config.threshAmp = m.getArgAsFloat(0);
        }
        else if (command == "/loud/max") {
            config.maxLoud = m.getArgAsFloat(0);
        }
        else if (command == "/loud/thresh") {
            config.threshLoud = m.getArgAsFloat(0);
        }
        else if (command == "/blendmode") {
            blendMode = static_cast<ofBlendMode>(m.getArgAsInt(0));
        }
        else if (command == "/bgcolor") {
            bgColor = ofColor(m.getArgAsInt(0), m.getArgAsInt(1), m.getArgAsInt(2));
        }
        else if (command == "/bgblendmode") {
            bgBlendMode = static_cast<ofBlendMode>(m.getArgAsInt(0));
        }
        else {
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
            if (all) {
                for (int i=0; i<layers.size(); i++) {
                    layerCommand(layers[i], command, m);
                }
            }
            else {
                if (idx > -1) {
                    layerCommand(layers[idx], command, m);
                }
            }
        }
        messageQueue.erase(messageQueue.begin());
    }
}

void ofApp::layerCommand(Layer &layer, string command, const ofxOscMessage &m) {
    if (command == "/load") {
        layer.load(m.getArgAsString(1));
    }
    else if (command == "/seek") {
        layer.seek(m.getArgAsFloat(1));
    }
    else if (command == "/seek/random") {
        layer.seek(ofRandom(1.f));
    }
    else if (command == "/reload") {
        layer.reload();
    }
    else if (command == "/noclear") {
        layer.setNoClear(m.getArgAsBool(1));
    }
    else if (command == "/bri") {
        layer.setBri(m.getArgAsFloat(1));
    }
    else if (command == "/alpha") {
        layer.setAlpha(m.getArgAsFloat(1));
    }
    else if (command == "/choose") {
        layer.choose();
    }
    else if (command == "/clear") {
        layer.clear();
    }
    else if (command == "/reset") {
        layer.reset();
    }
    else if (command == "/pos") {
        layer.pos = glm::vec2(m.getArgAsFloat(1), m.getArgAsFloat(2));
    }
    else if (command == "/size") {
        layer.size = glm::vec2(m.getArgAsFloat(1), m.getArgAsFloat(2));
    }
    else if (command == "/color") {
        layer.color = parseColor(m);
    }
    else if (command == "/color/random") {
        layer.color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
    }
    else if (command == "/color/lerp") {
        ofFloatColor fromColor = parseColor(m, 2);
        ofFloatColor toColor = parseColor(m, 5);
        float perc = m.getArgAsFloat(1);
        layer.color = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
    }
    else if (command == "/color/mfcc") {
        layer.useMFCC = m.getArgAsBool(1);
    }
    else if (command == "/data") {
        vector<string> ds;
        for (int i=1; i<m.getNumArgs(); i++) {
            ds.push_back(m.getArgAsString(i));
        }
        layer.setDataSources(ds);
    }
    else if (command == "/data/add") {
        vector<string> ds;
        for (int i=1; i<m.getNumArgs(); i++) {
            ds.push_back(m.getArgAsString(i));
        }
        layer.addDataSources(ds);
    }
    else if (command == "/unload") {
        layer.unload();
    }
    else if (command == "/speed") {
        layer.speed = m.getArgAsFloat(1);
    }
    else if (command == "/behaviour") {
        layer.behaviour = m.getArgAsInt(1);
    }
}

ofFloatColor ofApp::parseColor(const ofxOscMessage &m, int idx) {
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

//--------------------------------------------------------------
void ofApp::draw(){
    fbo.begin();
    ofClear(0, 0, 0, 0);
    switch (blendMode) {
        case OF_BLENDMODE_ALPHA:
            ofSetColor(255, 255, 255, 255 / layers.size());
            break;
        default:
            ofSetColor(255);
            break;
    }
    ofEnableBlendMode(blendMode);
    for (int i=0; i<layers.size(); i++) {
        layers[i].draw();
    }
    ofDisableBlendMode();
    fbo.end();
    
    ofEnableBlendMode(bgBlendMode);
    ofBackground(bgColor);
    ofSetColor(255);
    fbo.draw(0, 0);
    
    if (showDebug) {
        for (int i=0; i<layers.size(); i++) {
            ofSetColor(255);
            if (layers[i].gen != NULL) {
                layers[i].draw(20+i*120, ofGetHeight()-120, 100, 100);
            }
        }
    }
    ofDisableBlendMode();
}

void ofApp::exit() {
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch (key) {
        case 'f':
            ofToggleFullscreen();
            break;
        case '0':
            blendMode = OF_BLENDMODE_DISABLED;
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
            blendMode = static_cast<ofBlendMode>(key - '1' + 1);
            break;
        case 'd':
            showDebug = !showDebug;
            break;
        case 'r': {
            for (int i=0; i<layers.size(); i++) {
                ofxOscMessage m;
                layerCommand(layers[i], "/reload", m);
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
    fbo.allocate(ofGetWidth(), ofGetHeight());
    layoutLayers(layout);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
