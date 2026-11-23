#include "ofApp.h"
#include "Config.h"
#include <iostream>

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(frameRate);
	ofSetVerticalSync(true);
	ofBackground(0);
    
    ofDisableArbTex();

    bool preview = false;
    auto channel = std::make_shared<Logger>();
    ofSetLoggerChannel(channel);
#if VOSC_OPERATOR_UI
    preview = previewChild != nullptr;
    logger = std::move(channel);
#endif
    
    try {
#if VOSC_OPERATOR_UI
        if (!preview) overlay.setup();
#endif
        CommandSystem::ScenePrograms programs;
        if (startupScene) {
            programs = CommandSystem::prepareStartupScene(*startupScene);
            ofSetWindowTitle(preview ? "VOSC Preview — " + startupScene->title : startupScene->title);
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
            const auto marker = "VOSC_SCENE_READY frame=" + std::to_string(frame) +
                                " title=" + startupScene->title;
#if VOSC_OPERATOR_UI
            if (previewChild) {
                if (!previewChild->announceReady(marker)) {
                    failStartup("Could not report preview readiness to its owner.");
                    return;
                }
            } else {
                overlay.rememberScene(*startupScene, vosc);
            }
#endif
            finishStartup();
            std::cout << marker << std::endl;
            startupScene.reset();
        }, preview);
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
    if (previewChild) {
        if (!previewChild->poll()) {
            ofExit();
            return;
        }
    } else {
        overlay.update();
    }
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
    if (!previewChild) overlay.shutdown();
#endif
    vosc.exit();
}

void ofApp::keyPressed(int key) {
#if VOSC_OPERATOR_UI
    if (previewChild && key == 'f') return;
#endif
    vosc.keyPressed(key);
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
