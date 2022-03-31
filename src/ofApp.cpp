#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

    receiver.setup(33333);
	tidal = new ofxTidalCycles(1);
    setupSounds(NUM_SOUNDS);
    setupVisuals(NUM_VISUALS, L_STACK);
    
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
        if (m.getAddress() == "/sound/data") {
            int instNum = m.getArgAsInt(0);
            sounds[instNum].parse(m);
        }
        else if (m.getAddress() == "/dirt/play") {
            tidal->parse(m);
        }
        else if (m.getAddress() == "/setup/sound") {
            setupSounds(m.getArgAsInt(0));
        }
        else if (m.getAddress() == "/setup/vis") {
            setupVisuals(m.getArgAsInt(0), static_cast<Layout>(m.getArgAsInt(1)));
        }
        else if (m.getAddress() == "/setup/layout") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].layout(static_cast<Layout>(m.getArgAsInt(0)));
            }
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
        else if (m.getAddress() == "/config/color") {
            config.color = ofFloatColor(ofColor(m.getArgAsInt(0), m.getArgAsInt(1), m.getArgAsInt(2)));
        }
        else if (m.getAddress() == "/config/bgcolor") {
            bgColor = ofColor(m.getArgAsInt(0), m.getArgAsInt(1), m.getArgAsInt(2));
        }
        else if (m.getAddress() == "/config/bgblendmode") {
            bgBlendMode = static_cast<ofBlendMode>(m.getArgAsInt(0));
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
                visuals[i].shader.choose();
            }
        }
        else if (m.getAddress() == "/all/shader/reload") {
            reloadShaders();
        }
        else if (m.getAddress() == "/all/video") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].video.name = m.getArgAsString(0);
            }
        }
        else if (m.getAddress() == "/all/video/random") {
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].video.choose();
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
        else if (m.getAddress() == "/all/color") {
            ofFloatColor color = ofFloatColor(ofColor(m.getArgAsInt(0), m.getArgAsInt(1), m.getArgAsInt(2)));
            for (int i=0; i<visuals.size(); i++) {
                visuals[i].config.color = color;
            }
        }
        else if (m.getAddress() == "/vis/shader") {
            visuals[m.getArgAsInt(0)].shader.name = m.getArgAsString(1);
        }
        else if (m.getAddress() == "/vis/shader/random") {
            visuals[m.getArgAsInt(0)].shader.choose();
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
        else if (m.getAddress() == "/vis/color") {
            visuals[m.getArgAsInt(0)].config.color = ofFloatColor(ofColor(m.getArgAsInt(1), m.getArgAsInt(2), m.getArgAsInt(3)));
        }
        else if (m.getAddress() == "/vis/color/random") {
            visuals[m.getArgAsInt(0)].config.color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
        }
        else if (m.getAddress() == "/vis/behaviour") {
            visuals[m.getArgAsInt(0)].config.behaviour = m.getArgAsInt(1);
        }
    }
}

void ofApp::reloadShaders() {
    for (int i=0; i<visuals.size(); i++) {
        visuals[i].shader.reload();
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
        case 'r':
            reloadShaders();
            break;
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
