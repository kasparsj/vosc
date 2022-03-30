#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

    receiver.setup(33333);
	tidal = new ofxTidalCycles(1);
    setupSoundData(NUM_VISUALS);
    setupVisuals(NUM_VISUALS);
}

void ofApp::setupSoundData(int numInsts) {
    soundData.resize(numInsts);
}

void ofApp::setupVisuals(int numVisuals) {
    visuals.resize(numVisuals);
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].position = glm::vec2(0, ofGetHeight() / numVisuals * i);
        visuals[i].size = glm::vec2(ofGetWidth(), ofGetHeight() / numVisuals);
        visuals[i].sources.push_back("amp" + ofToString(i));
        visuals[i].behaviour = B_RANDOM_SHADER;
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    parseIncomingMessages();
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
	for (int i = 0; i < visuals.size(); i++) {
		visuals[i].update(soundData, tidal->notes);
	}
}

void ofApp::parseIncomingMessages(){
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage m;
        receiver.getNextMessage(m);
        if (m.getAddress() == "/sound/data") {
            int instNum = m.getArgAsInt(0);
            soundData[instNum].parse(m);
        }
        else if (m.getAddress() == "/dirt/play") {
            tidal->parse(m);
        }
        else if (m.getAddress() == "/sound/num") {
            setupSoundData(m.getArgAsInt(0));
        }
        else if (m.getAddress() == "/vis/num") {
            setupVisuals(m.getArgAsInt(0));
        }
        else if (m.getAddress() == "/vis/shader") {
            visuals[m.getArgAsInt(0)].shader.name = m.getArgAsString(1);
        }
        else if (m.getAddress() == "/vis/pos") {
            visuals[m.getArgAsInt(0)].position = glm::vec2(m.getArgAsFloat(1), m.getArgAsFloat(2));
        }
        else if (m.getAddress() == "/vis/size") {
            visuals[m.getArgAsInt(0)].size = glm::vec2(m.getArgAsFloat(1), m.getArgAsFloat(2));
        }
        else if (m.getAddress() == "/vis/behaviour") {
            visuals[m.getArgAsInt(0)].behaviour = m.getArgAsInt(1);
        }
    }
}

//--------------------------------------------------------------
void ofApp::draw(){
	ofSetColor(255);
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].draw();
    }
}

void ofApp::exit() {
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

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
