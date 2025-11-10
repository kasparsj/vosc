#include "TextureArrayDraw.h"

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
        texArrayShader.setUniformTexture("tex", tex, 0);
        texArrayShader.setUniform1i("texIndex", index);
        
        // Draw a quad with the shader using getQuad
        glm::vec3 p1(x, y, 0);
        glm::vec3 p2(x + w, y, 0);
        glm::vec3 p3(x + w, y + h, 0);
        glm::vec3 p4(x, y + h, 0);
        ofMesh quad = tex.getQuad(p1, p2, p3, p4);
        quad.draw();
        
        texArrayShader.end();
    } else {
        // Fallback: draw normally if shader failed to load
        tex.draw(x, y, w, h);
    }
}

