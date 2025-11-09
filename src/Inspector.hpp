#pragma once

#include "ofMain.h"
#include "OSCInput.h"
#include "Layer.h"
#include "ui/Button.h"

class Inspector {
public:
    Inspector();
    void inspect(const vector<shared_ptr<Layer>>& layers);
    
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
    void debugTexture(const ofTexture& tex, const string& name = "", float x = 0, float y = 0);
    void debugEmpty(string text);
    void drawInputs();
    void drawAmplitude(const shared_ptr<OSCInput> input);
    
    const vector<shared_ptr<Layer>>* layers;
    int debugLayer = -1;
    
    // Large texture view
    const ofTexture* largeTexture = nullptr;
    string largeTextureName;
    void drawLargeTexture();
    
    // Buttons
    Button printUniformsButton;
    vector<Button> textureButtons;
    vector<string> textureButtonNames;
    vector<const ofTexture*> textureButtonTextures;
    
    void updateTextureButtons();
    void clearTextureButtons();
};
