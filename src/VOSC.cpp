#include "VOSC.hpp"
#include "Config.h"

#if USE_OFX_HPVPLAYER
#include "ofxHPVPlayer.h"
#endif
#if USE_OFX_ULTRALIGHT
#include "ofxUltralight.h"
#endif

VOSC::VOSC()
    : resourceSystem()
    , runtime()
    , inputSystem()
    , renderSystem()
    , commandSystem(runtime, renderSystem, inputSystem) {}

void VOSC::setup(unsigned int port) {
    inputSystem.setup(port);
    resourceSystem.setup();
    runtime.camera.setup();
    renderSystem.setup(INITIAL_LAYERS);
    commandSystem.setup();
    renderSystem.windowResized(ofGetWidth(), ofGetHeight());

#ifdef TARGET_CPU_UNIVERSAL
    ofLog() << "ARM64: Yes";
#else
    ofLog() << "ARM64: No";
#endif
    ofLog() << ("Vendor :" + ofToString(glGetString(GL_VENDOR)));
    ofLog() << ("GPU : " + ofToString(glGetString(GL_RENDERER)));
    ofLog() << ("OpenGL ver. " + ofToString(glGetString(GL_VERSION)));
    ofLog() << ("GLSL ver. " + ofToString(glGetString(GL_SHADING_LANGUAGE_VERSION)));
}

void VOSC::update() {
    runtime.camera.preUpdate();
    commandSystem.update();
    runtime.inputs.update();
    while (runtime.tidal->notes.size() > MAX_NOTES) {
        runtime.tidal->notes.erase(runtime.tidal->notes.begin());
    }
    resourceSystem.update(runtime.tidal->notes);
    renderSystem.update(runtime.tidal->notes);
    runtime.camera.update();
#if USE_OFX_HPVPLAYER
    HPV::Update();
#endif
#if USE_OFX_ULTRALIGHT
    ofxUltralight::update();
#endif
}

void VOSC::draw() {
    renderSystem.draw(runtime.camera, showDebug);
}

void VOSC::mousePressed(int x, int y, int button) {
    renderSystem.mousePressed(x, y, button, showDebug);
}

void VOSC::mouseReleased(int x, int y, int button) {
    renderSystem.mouseReleased(x, y, button, showDebug);
}

void VOSC::keyPressed(int key) {
    switch (key) {
        case 'f':
            ofToggleFullscreen();
            break;
        case OF_KEY_TAB:
            showDebug = !showDebug;
            break;
        default:
            renderSystem.keyPressed(key, showDebug);
            break;
    }
}

void VOSC::windowResized(int w, int h) {
    renderSystem.windowResized(w, h);
}

void VOSC::exit() {
    renderSystem.shutdown();
    resourceSystem.shutdown();
    runtime.tidal.reset();
#if USE_OFX_HPVPLAYER
    HPV::DestroyHPVEngine();
#endif
}
