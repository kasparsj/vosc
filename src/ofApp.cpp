#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

    receiver.setup(33333);
	tidal = new ofxTidalCycles(1);
    setupSounds(MAX_SOUNDS);
    setupVisuals(MAX_VISUALS, L_STACK);
    
    fbo.allocate(ofGetWidth(), ofGetHeight());
}

void ofApp::setupSounds(int numInsts) {
    sounds.resize(numInsts);
}

void ofApp::setupVisuals(int numVisuals, Layout layout) {
    visuals.resize(numVisuals);
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].setup(i, numVisuals, sounds.size() > i ? "amp" + ofToString(i) : "");
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
        setupVisuals(m.getArgAsInt(0), static_cast<Layout>(m.getArgAsInt(1)));
    }
    else if (command == "/layout") {
        for (int i=0; i<visuals.size(); i++) {
            visuals[i].layout(static_cast<Layout>(m.getArgAsInt(0)));
        }
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
            visualCommand(visuals[std::stoi(which)], cmd + command.substr(firstSlash+2), m);
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
    else if (command == "/shader") {
        visual.shader.name = m.getArgAsString(0);
    }
    else if (command == "/shader/random") {
        visual.shader.choose();
    }
    else if (command == "/shader/reload") {
        visual.shader.reload();
    }
    else if (command == "/shader/noclear") {
        visual.shader.noClear = m.getArgAsBool(0);
    }
    else if (command == "/sketch") {
        visual.sketch.name = m.getArgAsString(0);
    }
    else if (command == "/sketch/random") {
        visual.sketch.choose();
    }
    else if (command == "/sketch/clear") {
        visual.sketch.clear();
    }
    else if (command == "/sketch/reset") {
        visual.sketch.reset();
    }
    else if (command == "/pos") {
        visual.pos = glm::vec2(m.getArgAsFloat(0), m.getArgAsFloat(1));
    }
    else if (command == "/size") {
        visual.size = glm::vec2(m.getArgAsFloat(0), m.getArgAsFloat(1));
    }
    else if (command == "/color") {
        ofFloatColor color;
        for (int i=0; i<m.getNumArgs(); i++) {
            if (m.getArgType(i) == OFXOSC_TYPE_FLOAT) {
                color[i] = m.getArgAsFloat(i);
            }
            else {
                color[i] = m.getArgAsInt(i) / 255.f;
            }
        }
        visual.color = color;
    }
    else if (command == "/color/random") {
        visual.color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
    }
    else if (command == "/behaviour") {
        visual.config.behaviour = m.getArgAsInt(0);
    }
    else if (command == "/data") {
        visual.dataSources.clear();
        for (int i=0; i<m.getNumArgs(); i++) {
            string dsName = m.getArgAsString(i);
            string dsMax = "";
            if (dsName.find(":") != string::npos) {
                dsMax = dsName.substr(dsName.find(":") + 1);
                dsName = dsName.substr(0, dsName.find(":"));
            }
            auto it = DataSourceMap.find(dsName);
            if (it != DataSourceMap.end()) {
                // todo: should hold struct instead of string
                visual.dataSources.push_back(it->first + dsMax);
            }
            else {
                ofLog() << "invalid data source " << m.getArgAsString(i);
            }
        }
    }
    else if (command == "/data/add") {
        for (int i=0; i<m.getNumArgs(); i++) {
            visual.dataSources.push_back(m.getArgAsString(i));
        }
    }
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

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
