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
    setupVisuals(MAX_VISUALS);
    windowResized(ofGetWidth(), ofGetHeight());
}

void ofApp::setupSounds(int numInsts) {
    sounds.resize(numInsts);
}

void ofApp::setupVisuals(int numVisuals) {
    visuals.resize(numVisuals);
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].setup(i, numVisuals, sounds.size() > i ? "amp" + ofToString(i) : "");
    }
}

void ofApp::layoutVisuals(Layout layout) {
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].layout(layout);
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    parseMessages();
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
	for (int i = 0; i < visuals.size(); i++) {
		visuals[i].update(sounds, tidal->notes, config);
	}
}

void ofApp::parseMessages(){
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage m;
        receiver.getNextMessage(m);
        parseMessage(m);
    }
    parseQueuedMessages();
}

void ofApp::parseMessage(const ofxOscMessage &m) {
    string command = m.getAddress();
    if (command == "/sound/data") {
        int instNum = m.getArgAsInt(0);
        sounds[instNum].parse(m);
    }
    else if (command == "/dirt/play") {
        tidal->parse(m);
    }
    else if (command == "/sounds") {
        setupSounds(m.getArgAsInt(0));
    }
    else if (command == "/visuals") {
        setupVisuals(m.getArgAsInt(0));
        if (m.getNumArgs() > 1) {
            layoutVisuals(static_cast<Layout>(m.getArgAsInt(1)));
        }
    }
    else {
        messageQueue.push_back(m);
    }
}

void ofApp::parseQueuedMessages() {
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
    if (isOnset) {
        while (messageQueue.size()) {
            ofxOscMessage &m = messageQueue[0];
            string command = m.getAddress();
            if (command == "/layout") {
                layoutVisuals(static_cast<Layout>(m.getArgAsInt(0)));
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
            else if (command == "/speed") {
                config.speed = m.getArgAsFloat(0);
            }
            else if (command == "/behaviour") {
                config.behaviour = m.getArgAsInt(0);
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
                string::size_type firstSlash = command.find("/", 1);
                string cmd = command.substr(0, firstSlash);
                string which = command.substr(firstSlash+1, 1);
                if (which == "*" || which == "x" || which == "a") {
                    for (int i=0; i<visuals.size(); i++) {
                        visualCommand(visuals[i], cmd + command.substr(firstSlash+2), m);
                    }
                }
                else {
                    int i = -1;
                    try {
                        i = std::stoi(which);
                    }
                    catch (...) {
                        ofLog() << "invalid command " << command;
                    }
                    if (i > -1) {
                        visualCommand(visuals[i], cmd + command.substr(firstSlash+2), m);
                    }
                }
            }
            messageQueue.erase(messageQueue.begin());
        }
    }
}

void ofApp::visualCommand(Visual &visual, string command, const ofxOscMessage &m) {
    if (command == "/video") {
        visual.video.name = m.getArgAsString(0);
    }
    else if (command == "/video/pos") {
        visual.video.pos = m.getArgAsFloat(0);
    }
    else if (command == "/video/pos/random") {
        visual.video.pos = ofRandom(1.f);
    }
    else if (command == "/video/bri") {
        visual.video.bri = m.getArgAsFloat(0);
    }
    else if (command == "/video/alpha") {
        visual.video.alpha = m.getArgAsFloat(0);
    }
    else if (command == "/shader") {
        visual.shader.name = m.getArgAsString(0);
    }
    else if (command == "/shader/choose") {
        visual.shader.choose();
    }
    else if (command == "/shader/reload") {
        visual.shader.reload();
    }
    else if (command == "/shader/noclear") {
        visual.shader.noClear = m.getArgAsBool(0);
    }
    else if (command == "/shader/bri") {
        visual.shader.bri = m.getArgAsFloat(0);
    }
    else if (command == "/shader/alpha") {
        visual.shader.alpha = m.getArgAsFloat(0);
    }
    else if (command == "/sketch") {
        visual.sketch.name = m.getArgAsString(0);
    }
    else if (command == "/sketch/choose") {
        visual.sketch.choose();
    }
    else if (command == "/sketch/clear") {
        visual.sketch.clear();
    }
    else if (command == "/sketch/reset") {
        visual.sketch.reset();
    }
    else if (command == "/sketch/bri") {
        visual.sketch.bri = m.getArgAsFloat(0);
    }
    else if (command == "/sketch/alpha") {
        visual.sketch.alpha = m.getArgAsFloat(0);
    }
    else if (command == "/pos") {
        visual.pos = glm::vec2(m.getArgAsFloat(0), m.getArgAsFloat(1));
    }
    else if (command == "/size") {
        visual.size = glm::vec2(m.getArgAsFloat(0), m.getArgAsFloat(1));
    }
    else if (command == "/color") {
        visual.color = parseColor(m);
    }
    else if (command == "/color/random") {
        visual.color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
    }
    else if (command == "/color/lerp") {
        ofFloatColor fromColor = parseColor(m, 1);
        ofFloatColor toColor = parseColor(m, 4);
        float perc = m.getArgAsFloat(0);
        visual.color = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
    }
    else if (command == "/color/mfcc") {
        visual.useMFCC = m.getArgAsBool(0);
    }
    else if (command == "/behaviour") {
        visual.config.behaviour = m.getArgAsInt(0);
    }
    else if (command == "/data") {
        vector<string> ds;
        for (int i=0; i<m.getNumArgs(); i++) {
            ds.push_back(m.getArgAsString(i));
        }
        visual.setDataSources(ds);
    }
    else if (command == "/data/add") {
        vector<string> ds;
        for (int i=0; i<m.getNumArgs(); i++) {
            ds.push_back(m.getArgAsString(i));
        }
        visual.addDataSources(ds);
    }
    else if (command == "/unload") {
        visual.unload();
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
            ofSetColor(255, 255, 255, 255 / visuals.size());
            break;
        default:
            ofSetColor(255);
            break;
    }
    ofEnableBlendMode(blendMode);
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].draw();
    }
    ofDisableBlendMode();
    fbo.end();
    
    ofEnableBlendMode(bgBlendMode);
    ofBackground(bgColor);
    ofSetColor(255);
    fbo.draw(0, 0);
    
    if (showDebug) {
        for (int i=0; i<visuals.size(); i++) {
            ofSetColor(255);
            if (visuals[i].shader.isEnabled() && visuals[i].sketch.isEnabled()) {
                visuals[i].shader.fbo.draw(20+i*120, ofGetHeight()-120, 100, 50);
                visuals[i].sketch.fbo.draw(20+i*120, ofGetHeight()-70, 100, 50);
            }
            else {
                visuals[i].shader.fbo.draw(20+i*120, ofGetHeight()-120, 100, 100);
                visuals[i].sketch.fbo.draw(20+i*120, ofGetHeight()-120, 100, 100);
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
            for (int i=0; i<visuals.size(); i++) {
                ofxOscMessage m;
                visualCommand(visuals[i], "/shader/reload", m);
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
    layoutVisuals(layout);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
