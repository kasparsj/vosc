#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "Layer.h"
#include "ResourceRegistry.hpp"
#include "RuntimeContext.hpp"
#include "ofxPostProcessing.h"
#include "ofxMidi.h"
#include "inspector/Inspector.hpp"
#include "ofxDeferredShading.h"
#include "osc/CommandParser.hpp"
#include "osc/CommandRouter.hpp"
#include <deque>
#include <memory>

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
    void applyLayersPayload(const osc::LayersPayload& layersPayload);
    void resetLayers(const osc::LayersPayload& layersPayload);
    void applyShadingPasses(const vector<osc::ShadingPassSpec>& passes);
    
    void beginDraw();
    void doDraw();
    void endDraw();
    
    void parseMessages();
    bool checkOnset();
    void parseMessage(const ofxOscMessage& m);
    void processQueue();
    void invalidCommand(const ofxOscMessage& m);
    void invalidCommand(const osc::ParseError& error);
    bool isQueuedCommand(osc::CommandType type) const;
    void setupCommandRouter();
    void routeTargetedResource(const osc::Command& command);
    void handleMidi(const osc::Command& command);

    void allLayersCommand(string command, const ofxOscMessage& m);
    template<typename T>
    void createShadingPass(T& processor, string passName);
    template<typename T>
    void createShadingPass(T& processor, int passId);
    template<typename T>
    void createShadingPass(T& processor, PostPass passId);
    
    ofxOscReceiver receiver;
    std::deque<osc::Command> messageQueue;
    osc::CommandParser commandParser;
    osc::CommandRouter commandRouter;
    
    ResourceRegistry resources;
    RuntimeContext runtime;
    vector<shared_ptr<Layer>> layers = {};
    Layout layout = Layout::STACK;
    ofxMidiIn midiIn;
    Inspector inspector;
    
    ofxPostProcessing post;
    ofxDeferredProcessing deferred;
    bool deferredShading = false;
    ofxDeferred::PointLightPass::Ptr pointLightPass;
    ofxDeferred::ShadowLightPass::Ptr shadowLightPass;
    
    int waitOnset = -1;
    bool forceOnset = false;

};
