#pragma once

#include "ofMain.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "Layer.h"
#include "Config.h"
#include "ofxPostProcessing.h"
#include "Texture.h"
#include "Variable.h"
#include "Lights.h"
#include "ofxMidi.h"
#include "ofxImGui.h"
#include "Logger.h"

class ofApp : public ofBaseApp {

public:
	void setup();
    void setupLayers(int numLayers);
    void layoutLayers(Layout layout, const vector<Layer*> &layers);
	void update();
    void parseMessages();
    void parseMessage(const ofxOscMessage &m);
    void processQueue();
    bool checkOnset();
	void draw();
    void drawDebug();
    void drawGLVersion();
    void drawConsole();
    void drawGeoms();
    void drawTextures();
    void drawMics();
    void drawSounds();
    void drawAmplitude(Mic& sound);
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
    void micsCommand(string command, const ofxOscMessage& m);
    void micCommand(Mic& mic, string command, const ofxOscMessage& m);
    void midiCommand(string command, const ofxOscMessage& m);
    void soundsCommand(string command, const ofxOscMessage& m);
    void soundCommand(Sound& sound, string command, const ofxOscMessage& m);
    void textureCommand(Texture* tex, string command, const ofxOscMessage& m);
    void texDataCommand(TexData& data, string command, const ofxOscMessage& m);
    void texDataColor(TexData& data, const ofxOscMessage &m);
    void shaderCommand(Shader& shader, string command, const ofxOscMessage& m);
    void geomCommand(Geom* geom, string command, const ofxOscMessage& m);
    void materialCommand(ofMaterialSettings& matSettings, string command, const ofxOscMessage& m);
    
    void createPostPass(string passName);
    void createPostPass(int passId);
    
    ofxOscReceiver receiver;
    vector<ofxOscMessage> messageQueue;
	ofxTidalCycles *tidal;
    
    vector<Mic> mics;
    vector<Sound> sounds;
    vector<Layer*> layers = {0};
    int waitOnset = -1;
    bool forceOnset;
    Layout layout = Layout::STACK;
    ofCamera* cam;
    ofxPostProcessing post;
    Variable* camPos;
    Variable* camLook;
    float camOrbit = 0;
    float camOrbitPerSecond = 0;
    Lights lights;
    ofxMidiIn midiIn;
    
    std::shared_ptr<Logger> console;
    ofxImGui::Gui gui;
    
    bool showDebug = false;
    bool showConsole = false;
};
