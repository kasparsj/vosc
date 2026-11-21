#include "ofApp.h"
#include "Config.h"
#include <iostream>

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
    
    try {
        CommandSystem::ScenePrograms programs;
        if (startupScene) {
            programs = CommandSystem::prepareStartupScene(*startupScene);
            ofSetWindowTitle(startupScene->title);
        }
        vosc.setup(oscPort, startupScene ? &*startupScene : nullptr, programs,
                   [this](int frame, const std::string& detail) {
            if (frame < 0) {
                failStartup(detail);
                return;
            }
            for (size_t i = 0; i < startupScene->commands.size(); ++i) {
                const auto& command = startupScene->commands[i];
                if (!command.asset.empty() && !scene::assetMatches(command)) {
                    failStartup(scene::commandContext(i, command.message) +
                                ": asset changed during setup: " + command.asset.string());
                    return;
                }
            }
            std::cout << "VOSC_SCENE_READY frame=" << frame
                      << " title=" << startupScene->title << std::endl;
            startupScene.reset();
        });
    } catch (const std::exception& error) {
        failStartup(error.what());
    }
}

void ofApp::failStartup(const std::string& detail) {
    std::cerr << "vosc: ";
    if (startupScene) std::cerr << startupScene->path.string() << ": ";
    std::cerr << detail << std::endl;
    startupFailed = true;
    ofExit(EXIT_FAILURE);
}

//--------------------------------------------------------------
void ofApp::update(){
    if (!startupFailed) vosc.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
    if (startupFailed) return;
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
