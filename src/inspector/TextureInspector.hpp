#pragma once

#include "ofMain.h"
#include "../ui/Button.h"
#include "../Texture.h"
#include "../Shader.h"
#include "TextureArrayDraw.h"

class TextureInspector {
public:
    TextureInspector();
    ~TextureInspector();
    void draw();
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void keyPressed(int key);
    
    void setTexture(const ofTexture* texture, const string& name = "");
    void setTexture(Texture* texture, const string& name = "");
    void clear();
    bool isEmpty() const { return texture == nullptr && textureObj == nullptr; }
    
    // Callback for opening shader inspector
    std::function<void(Shader*, const string&)> onOpenShaderInspector;
    
private:
    void drawTextureInfo();
    void drawTexturePreview();
    void drawLargeTexture();
    void drawArrayTexture(float x, float y, float width, float height);
    void drawShaderPasses();
    
    const ofTexture* texture = nullptr;
    Texture* textureObj = nullptr; // Full texture object to check if it's Shader-based
    string textureName;
    
    bool showLargeView = false;
    Button backButton;
    Button shaderButton;
    Button isStaticButton;
    Button needsUpdateButton;
    
    // Texture array support
    int currentLayerIndex = 0;
    TextureArrayDraw textureArrayDraw;
};

