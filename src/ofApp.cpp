#include "ofApp.h"
#include "Config.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(FRAME_RATE);
	ofSetVerticalSync(true);
	ofBackground(0);
    
#if USE_OFX_IMGUI
    gui.setup();
#endif
    ofDisableArbTex();

    console = std::make_shared<Logger>();
    ofSetLoggerChannel(console);
    
    vosc.setup(PORT);
}

//--------------------------------------------------------------
void ofApp::update(){
    vosc.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
    vosc.draw();
    drawConsole();
}

void ofApp::drawConsole() {
    if (console->hasNewErrors()) {
        showConsole = true;
    }
    if (vosc.showDebug || showConsole) {
#if USE_OFX_IMGUI
        gui.begin();
        
        console->draw(&showConsole);

        gui.end();
#endif
    }
}

void ofApp::exit() {
    vosc.exit();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch (key) {
        case 'c':
            showConsole = !showConsole;
            break;
        default:
            vosc.keyPressed(key);
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
    vosc.mousePressed(x, y, button);
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
    vosc.mouseReleased(x, y, button);
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
    vosc.windowResized(w, h);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
