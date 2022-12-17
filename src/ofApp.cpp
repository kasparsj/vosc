#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);
    
    gui.setup();
    ofDisableArbTex();

    receiver.setup(33333);
    
    console = std::make_shared<Logger>();
    ofSetLoggerChannel(console);
}

//--------------------------------------------------------------
void ofApp::update(){
    receiver.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
    receiver.draw();
    drawConsole();
}

void ofApp::drawConsole() {
    if (console->hasNewErrors()) {
        showConsole = true;
    }
    if (receiver.showDebug || showConsole) {
        gui.begin();
        
        console->draw(&showConsole);

        gui.end();
    }
}

void ofApp::exit() {
    receiver.exit();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch (key) {
        case 'c':
            showConsole = !showConsole;
            break;
        default:
            receiver.keyPressed(key);
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
    receiver.windowResized(w, h);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
