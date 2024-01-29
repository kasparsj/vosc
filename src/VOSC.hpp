#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxTidalCycles.h"
#include "Layer.h"
#include "Camera.hpp"
#include "ofxPostProcessing.h"
#include "ofxMidi.h"
#include "Inspector.hpp"
#include "ofxDeferredShading.h"

class VOSC {
public:
    void setup(unsigned int port);
    void update();
    void draw();
    void exit();
    
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void keyPressed(int key);
    void windowResized(int w, int h);
    
    bool showDebug = false;
    
private:
    void setupLayers(int numLayers);
    void layoutLayers(Layout layout);
    void resetLayers(const ofxOscMessage& m);
    
    void beginDraw();
    void doDraw();
    void endDraw();
    
    void parseMessages();
    bool checkOnset();
    void parseMessage(const ofxOscMessage& m);
    void processQueue();
    void invalidCommand(const ofxOscMessage& m);

    void shadingCommand(const string& command, const ofxOscMessage& m);
    void allLayersCommand(string command, const ofxOscMessage& m);
    void layersCommand(string command, const ofxOscMessage& m);
    void lightCommand(string command, const ofxOscMessage& m);
    void midiCommand(string command, const ofxOscMessage& m);
    template<typename T>
    void createShadingPass(T& processor, string passName);
    template<typename T>
    void createShadingPass(T& processor, int passId);
    template<typename T>
    void createShadingPass(T& processor, PostPass passId);
    
    ofxOscReceiver receiver;
    vector<ofxOscMessage> messageQueue;
    
    Camera camera;
    vector<Layer*> layers = {};
    Layout layout = Layout::STACK;
    ofxTidalCycles* tidal;
    ofxMidiIn midiIn;
    Inspector inspector;
    
    ofxPostProcessing post;
    ofxDeferredProcessing deferred;
    bool deferredShading = false;
    ofxDeferred::PointLightPass::Ptr pointLightPass;
    ofxDeferred::ShadowLightPass::Ptr shadowLightPass;
    
    int waitOnset = -1;
    bool forceOnset;

};
