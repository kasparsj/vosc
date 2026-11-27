#include "ofApp.h"
#include "Config.h"
#include <iostream>

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(frameRate);
	ofSetVerticalSync(true);
	ofBackground(0);
    
    ofDisableArbTex();

    auto channel = std::make_shared<Logger>();
    ofSetLoggerChannel(channel);
#if VOSC_OPERATOR_UI
    logger = std::move(channel);
#endif
    
    try {
#if VOSC_OPERATOR_UI
        if (oscPort) overlay.setup(*oscPort);
#endif
        CommandSystem::ScenePrograms programs;
        if (startupScene) {
            programs = CommandSystem::prepareStartupScene(*startupScene);
            ofSetWindowTitle(startupScene->title);
        }
        if (!oscPort && !startupScene) ofSetWindowTitle(draftSamples ? "VOSC Draft Preview" : "VOSC Shader Validation");
        vosc.setup(oscPort, draftSamples, startupScene ? &*startupScene : nullptr, programs,
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
            const auto marker = "VOSC_SCENE_READY frame=" + std::to_string(frame) +
                                " title=" + startupScene->title;
#if VOSC_OPERATOR_UI
            if (oscPort) overlay.rememberScene(*startupScene, vosc);
#endif
            finishStartup();
            std::cout << marker << std::endl;
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
    finishStartup();
    startupFailed = true;
    ofExit(EXIT_FAILURE);
}

//--------------------------------------------------------------
void ofApp::update(){
    if (startupFailed) return;
#if VOSC_OPERATOR_UI
    overlay.update();
#endif
    vosc.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
    if (startupFailed) return;
    vosc.draw();
#if VOSC_OPERATOR_UI
    if (overlay.isOpen()) overlay.draw(vosc, *logger);
#endif
}

void ofApp::exit() {
#if VOSC_OPERATOR_UI
    overlay.shutdown();
#endif
    vosc.exit();
}

void ofApp::keyPressed(int key) {
    if (oscPort) vosc.keyPressed(key);
}

void ofApp::windowResized(int w, int h){
    vosc.windowResized(w, h);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
