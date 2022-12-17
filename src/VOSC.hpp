#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxTidalCycles.h"
#include "Layer.h"
#include "OSCInput.h"
#include "Camera.hpp"
#include "ofxPostProcessing.h"
#include "ofxMidi.h"
#include "Inspector.hpp"

class VOSC {
public:
    void setup(unsigned int port);
    void update();
    void draw();
    void exit();
    
    void keyPressed(int key);
    void windowResized(int w, int h);
    
    bool showDebug = false;
    
private:
    void setupLayers(int numLayers);
    void layoutLayers(Layout layout);
    
    void beginDraw();
    void endDraw();
    
    void parseMessages();
    bool checkOnset();
    void parseMessage(const ofxOscMessage& m);
    void processQueue();
    void invalidCommand(const ofxOscMessage& m);

    void allLayersCommand(string command, const ofxOscMessage& m);
    void layersCommand(string command, const ofxOscMessage& m);
    void lightCommand(string command, const ofxOscMessage& m);
    void inputsCommand(string command, const ofxOscMessage& m);
    void midiCommand(string command, const ofxOscMessage& m);
    void createPostPass(string passName);
    void createPostPass(int passId);
    
    ofxOscReceiver receiver;
    vector<ofxOscMessage> messageQueue;
    
    Camera camera;
    vector<Layer*> layers = {0};
    Layout layout = Layout::STACK;
    vector<OSCInput> inputs;
    ofxTidalCycles* tidal;
    ofxPostProcessing post;
    ofxMidiIn midiIn;
    Inspector inspector;
    
    int waitOnset = -1;
    bool forceOnset;

};
