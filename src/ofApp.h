#pragma once

#include "ofMain.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "Layer.h"
#include "Config.h"
#include "ofxEasing.h"
#include "ofxPostProcessing.h"
#include "Texture.h"
#include "Lights.h"

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
    void layoutLayers(Layout layout, const vector<Layer*> &layers);
	void update();
    void parseMessages();
    void parseMessage(const ofxOscMessage &m);
    void processQueue();
    bool checkOnset();
	void draw();
    void drawDebug();
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
    
    void layersCommand(string command, const ofxOscMessage& m);
    void cameraCommand(string command, const ofxOscMessage& m);
    void lightCommand(string command, const ofxOscMessage& m);
    void layerCommand(Layer* layer, string command, const ofxOscMessage& m);
    void indexCommand(Layer *layer, string command, const ofxOscMessage& m);
    void allLayersCommand(string command, const ofxOscMessage& m);
    void soundsCommand(string command, const ofxOscMessage& m);
    void soundCommand(Sound& sound, string command, const ofxOscMessage& m);
    void textureCommand(Texture* tex, string command, const ofxOscMessage& m);
    void texDataCommand(TexData& data, string command, const ofxOscMessage& m);
    void shaderCommand(Shader& shader, string command, const ofxOscMessage& m);
    void geomCommand(Geom* geom, string command, const ofxOscMessage& m);
    void materialCommand(ofMaterialSettings& matSettings, string command, const ofxOscMessage& m);
    
    void handleFloat(float *value, const ofxOscMessage &m);
    void handlePercent(float *value, const ofxOscMessage &m);
    void handleVec3(glm::vec3 *value, const ofxOscMessage &m, int firstArg = 1);
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
    void createPostPass(string passName);

    ofxOscReceiver receiver;
    vector<ofxOscMessage> messageQueue;
	ofxTidalCycles *tidal;
    
    vector<Sound> sounds;
    vector<Layer*> layers = {0};
    int waitOnset = -1;
    bool forceOnset;
    Layout layout = Layout::STACK;
    map<float*, Tween<float>> floatTweens;
    map<glm::vec3*, Tween<glm::vec3>> vec3Tweens;
    map<ofFloatColor*, Tween<ofFloatColor>> colorTweens;
    ofCamera* cam;
    ofxPostProcessing post;
    glm::vec3 camPos = glm::vec3(0, 0, -870);
    glm::vec3 camLook = glm::vec3(0);
    Lights lights;
    
    bool showDebug = false;
};
