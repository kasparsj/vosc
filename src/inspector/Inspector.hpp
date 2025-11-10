#pragma once

#include "ofMain.h"
#include "../input/OSCInput.h"
#include "../layer/Layer.h"
#include "../ui/Button.h"
#include "TextureInspector.hpp"
#include "ShaderInspector.hpp"
#include "TextureArrayDraw.h"

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
    void debugGlobalShaders();
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
    vector<Button> textureButtons;
    vector<string> textureButtonNames;
    vector<const ofTexture*> textureButtonTextures;
    
    // Active inspectors
    TextureInspector textureInspector;
    ShaderInspector shaderInspector;
    bool showingTextureInspector = false;
    bool showingShaderInspector = false;
    
    // Texture array drawing
    TextureArrayDraw textureArrayDraw;
    
    void updateTextureButtons();
    void clearTextureButtons();
};
