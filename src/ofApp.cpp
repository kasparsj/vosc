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
        visuals[i].setup(i, numVisuals);
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    parseIncomingMessages();
    while (tidal->notes.size() > MAX_NOTES) {
        tidal->notes.erase(tidal->notes.begin());
    }
	for (int i = 0; i < visuals.size(); i++) {
        visuals[i].config = config; // todo: merge instead
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
        else if (m.getAddress() == "/setup/sound") {
            setupSoundData(m.getArgAsInt(0));
        }
        else if (m.getAddress() == "/setup/vis") {
            setupVisuals(m.getArgAsInt(0));
        }
        else if (m.getAddress() == "/config/amp") {
            config.maxAmp = m.getArgAsFloat(0);
        }
        else if (m.getAddress() == "/config/loud") {
            config.maxLoud = m.getArgAsFloat(0);
        }
        else if (m.getAddress() == "/config/speed") {
            config.speed = m.getArgAsFloat(0);
        }
        else if (m.getAddress() == "/config/behaviour") {
            config.behaviour = m.getArgAsInt(0);
        }
        else if (m.getAddress() == "/all/data/tidal") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].datas.clear();
                visuals[i].datas.push_back("tidal" + ofToString(i));
            }
        }
        else if (m.getAddress() == "/all/data/amp") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].datas.clear();
                visuals[i].datas.push_back("amp" + ofToString(i));
            }
        }
        else if (m.getAddress() == "/all/data/loud") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].datas.clear();
                visuals[i].datas.push_back("loud" + ofToString(i));
            }
        }
        else if (m.getAddress() == "/all/shader") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].shader.name = m.getArgAsString(0);
            }
        }
        else if (m.getAddress() == "/all/shader/random") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].shader.random();
            }
        }
        else if (m.getAddress() == "/all/shader/reload") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].shader.reload();
            }
        }
        else if (m.getAddress() == "/all/video") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].video.name = m.getArgAsString(0);
            }
        }
        else if (m.getAddress() == "/all/video/random") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].video.random();
            }
        }
        else if (m.getAddress() == "/all/video/pos/random") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].video.pos = ofRandom(1.f);
            }
        }
        else if (m.getAddress() == "/all/pos") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].pos = glm::vec2(m.getArgAsFloat(0), m.getArgAsFloat(1));
            }
        }
        else if (m.getAddress() == "/all/size") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].size = glm::vec2(m.getArgAsFloat(0), m.getArgAsFloat(1));
            }
        }
        else if (m.getAddress() == "/vis/shader") {
            visuals[m.getArgAsInt(0)].shader.name = m.getArgAsString(1);
        }
        else if (m.getAddress() == "/vis/shader/random") {
            visuals[m.getArgAsInt(0)].shader.random();
        }
        else if (m.getAddress() == "/vis/video") {
            visuals[m.getArgAsInt(0)].video.name = m.getArgAsString(1);
        }
        else if (m.getAddress() == "/vis/video/pos") {
            visuals[m.getArgAsInt(0)].video.pos = m.getArgAsFloat(1);
        }
        else if (m.getAddress() == "/vis/video/pos/random") {
            visuals[m.getArgAsInt(0)].video.pos = ofRandom(1.f);
        }
        else if (m.getAddress() == "/vis/pos") {
            visuals[m.getArgAsInt(0)].pos = glm::vec2(m.getArgAsFloat(1), m.getArgAsFloat(2));
        }
        else if (m.getAddress() == "/vis/size") {
            visuals[m.getArgAsInt(0)].size = glm::vec2(m.getArgAsFloat(1), m.getArgAsFloat(2));
        }
        else if (m.getAddress() == "/vis/behaviour") {
            visuals[m.getArgAsInt(0)].config.behaviour = m.getArgAsInt(1);
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
