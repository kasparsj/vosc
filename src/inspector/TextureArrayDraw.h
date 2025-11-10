#pragma once

#include "ofMain.h"

class TextureArrayDraw {
public:
    TextureArrayDraw();
    ~TextureArrayDraw();
    
    void draw(ofTexture& tex, int x, int y, int w, int h, int index = 0);
    
private:
    void loadShader();
    
    ofShader texArrayShader;
    bool shaderLoaded = false;
};

