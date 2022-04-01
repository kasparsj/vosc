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
        visuals[i].setup(i, numVisuals, sounds.size() > i ? "amp" + ofToString(i) : "static");
        visuals[i].layout(layout);
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    parseIncomingMessages();
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
	for (int i = 0; i < visuals.size(); i++) {
		visuals[i].update(sounds, tidal->notes, config);
	}
}

void ofApp::parseIncomingMessages(){
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage m;
        receiver.getNextMessage(m);
        if (m.getAddress().substr(0, 4) == "/all") {
            for (int i=0; i<visuals.size(); i++) {
                visualCommand(visuals[i], m.getAddress().substr(4), m, 0);
            }
        }
        else {
            doCommand(m.getAddress(), m);
        }
    }
}

void ofApp::doCommand(string command, ofxOscMessage &m) {
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
    else if (command == "/color") {
        config.color = ofFloatColor(ofColor(m.getArgAsInt(0), m.getArgAsInt(1), m.getArgAsInt(2)));
    }
    else if (command == "/bgcolor") {
        bgColor = ofColor(m.getArgAsInt(0), m.getArgAsInt(1), m.getArgAsInt(2));
    }
    else if (command == "/bgblendmode") {
        bgBlendMode = static_cast<ofBlendMode>(m.getArgAsInt(0));
    }
    else {
        visualCommand(visuals[m.getArgAsInt(0)], m.getAddress(), m, 1);
    }
}

void ofApp::visualCommand(Visual &visual, string command, ofxOscMessage &m, int i) {
    if (command == "/video") {
        visual.video.name = m.getArgAsString(i);
    }
    else if (command == "/video/pos") {
        visual.video.pos = m.getArgAsFloat(i);
    }
    else if (command == "/video/pos/random") {
        visual.video.pos = ofRandom(1.f);
    }
    else if (command == "/shader") {
        visual.shader.name = m.getArgAsString(i);
    }
    else if (command == "/shader/random") {
        visual.shader.choose();
    }
    else if (command == "/shader/reload") {
        visual.shader.reload();
    }
    else if (command == "/shader/noclear") {
        visual.shader.noClear = m.getArgAsBool(i);
    }
    else if (command == "/sketch") {
        visual.sketch.name = m.getArgAsString(i);
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
        visual.pos = glm::vec2(m.getArgAsFloat(i), m.getArgAsFloat(i+1));
    }
    else if (command == "/size") {
        visual.size = glm::vec2(m.getArgAsFloat(i), m.getArgAsFloat(i+1));
    }
    else if (command == "/color") {
        visual.config.color = ofFloatColor(ofColor(m.getArgAsInt(i), m.getArgAsInt(i+1), m.getArgAsInt(i+2)));
    }
    else if (command == "/color/random") {
        visual.config.color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
    }
    else if (command == "/behaviour") {
        visual.config.behaviour = m.getArgAsInt(i);
    }
    else if (command == "/data") {
        visual.sources.clear();
        for (int j=i; j<m.getNumArgs(); j++) {
            visual.sources.push_back(m.getArgAsString(j));
        }
    }
    else if (command == "/data/add") {
        for (int j=i; j<m.getNumArgs(); j++) {
            visual.sources.push_back(m.getArgAsString(j));
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
            visuals[i].shader.fbo.draw(20+i*120, ofGetHeight()-120, 100, 100);
        }
    }
    ofDisableBlendMode();
}

void ofApp::exit() {
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch (key) {
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
                visualCommand(visuals[i], "/shader/reload", m, 0);
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
