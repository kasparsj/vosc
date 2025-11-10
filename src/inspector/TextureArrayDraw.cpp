#include "TextureArrayDraw.h"
#include "utils.h"

TextureArrayDraw::TextureArrayDraw() : shaderLoaded(false) {
}

TextureArrayDraw::~TextureArrayDraw() {
    // Shader will be cleaned up automatically
}

void TextureArrayDraw::loadShader() {
    if (shaderLoaded) return;
    
    // Load the tex_array shader with passthru vertex shader
    // The passthru.vert provides the VertexAttrib structure that tex_array.frag expects
    if (texArrayShader.load("shaders/passthru.vert", "shaders/array/tex.frag")) {
        shaderLoaded = true;
    } else {
        ofLogWarning("TextureArrayDraw") << "Could not load tex_array shader";
    }
}

void TextureArrayDraw::draw(ofTexture& tex, int x, int y, int w, int h, int index) {
    loadShader();
    
    if (shaderLoaded) {
        texArrayShader.begin();
        texArrayShader.setUniformTexture("tex", GL_TEXTURE_2D_ARRAY, tex.getTextureData().textureID, 0);
        texArrayShader.setUniform1i("texIndex", index);
        
        // Draw a quad with the shader using getQuad
        getQuad(tex, x, y, w, h).draw();
        
        texArrayShader.end();
    } else {
        // Fallback: draw normally if shader failed to load
        tex.draw(x, y, w, h);
    }
}

