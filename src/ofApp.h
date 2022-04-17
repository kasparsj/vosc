#pragma once

#include "ofMain.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "Layer.h"
#include "Config.h"
#include "ofxEasing.h"
#include "ofxPostProcessing.h"

template <typename T>
struct Tween {
    T from;
    T to;
    float dur;
    float start;
    ofxeasing::function ease;
};

class ofApp : public ofBaseApp {

public:
	void setup();
    void setupSounds(int numInsts);
    void setupLayers(int numLayers);
    void layoutLayers(Layout layout);
	void update();
    void parseMessages();
    void parseMessage(const ofxOscMessage &m);
    void processQueue();
    bool checkOnset();
	void draw();
	void exit();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);
    
    void layerCommand(Layer &layer, string command, const ofxOscMessage &m);
    void allLayersCommand(string command, const ofxOscMessage &m);
    void soundCommand(Sound &sound, string command, const ofxOscMessage &m);
    void handleFloat(float *value, const ofxOscMessage &m);
    void handleVec3(glm::vec3 *value, const ofxOscMessage &m);
    void handleColor(ofFloatColor *value, const ofxOscMessage &m);
    void createTween(float *value, float target, float dur, ofxeasing::function ease);
    void createTween(float *value, float target, float dur, string ease) {
        // todo: implement
        createTween(value, target, dur, ofxeasing::linear::easeNone);
    }
    void createTween(float *value, float target, float dur) {
        createTween(value, target, dur, ofxeasing::linear::easeNone);
    }
    void createTween(float *value, float target) {
        createTween(value, target, 1.0, ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3 *value, const glm::vec3 &target, float dur, ofxeasing::function ease);
    void createTween(glm::vec3 *value, const glm::vec3 &target, float dur, string ease) {
        // todo: implement
        createTween(value, target, dur, ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3 *value, const glm::vec3 &target, float dur) {
        createTween(value, target, dur, ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3 *value, const glm::vec3 &target) {
        createTween(value, target, 1.0, ofxeasing::linear::easeNone);
    }
    void createTween(ofFloatColor *value, const ofFloatColor &target, float dur, ofxeasing::function ease);
    void createTween(ofFloatColor *value, const ofFloatColor &target, float dur, string ease) {
        // todo: implement
        createTween(value, target, dur, ofxeasing::linear::easeNone);
    }
    void createTween(ofFloatColor *value, const ofFloatColor &target, float dur) {
        createTween(value, target, dur, ofxeasing::linear::easeNone);
    }
    void createTween(ofFloatColor *value, const ofFloatColor &target) {
        createTween(value, target, 1.0, ofxeasing::linear::easeNone);
    }
    void updateFloats();
    void updateVecs();
    void updateColors();

    ofxOscReceiver receiver;
    vector<ofxOscMessage> messageQueue;
	ofxTidalCycles *tidal;
    
    vector<Sound> sounds;
    vector<Layer> layers;
    int waitOnset = -1;
    bool forceOnset;
    
    ofFbo fbo;
    bool showDebug = false;
    Layout layout = L_STACK;
    map<float*, Tween<float>> floatTweens;
    map<glm::vec3*, Tween<glm::vec3>> vec3Tweens;
    map<ofFloatColor*, Tween<ofFloatColor>> colorTweens;
    ofCamera cam;
    ofxPostProcessing post;
    bool useCam = false;
};
