#pragma once

#include "ofMain.h"
#include "OSCInput.h"
#include "Layer.h"

class Inspector {
public:
    void inspect(const vector<Layer*>& layers, const vector<OSCInput>& inputs);
    
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void keyPressed(int key);

private:
    void drawGLVersion();
    void drawConsole();
    void drawDebugGlobals();
    void drawDebugLayer(int i);
    void debugGeoms();
    void debugGeom(int i);
    void debugGlobalTextures();
    void debugShaderTextures(int i);
    void debugShaderBuffers(int i);
    void debugTexture(const ofTexture& tex);
    void debugEmpty(string text);
    void drawInputs();
    void drawAmplitude(const OSCInput& input);
    
    const vector<Layer*>* layers;
    const vector<OSCInput>* inputs;
    int debugLayer = -1;
    
};
