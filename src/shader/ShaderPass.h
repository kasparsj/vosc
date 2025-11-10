#pragma once

#include "ofMain.h"
#include "../tex/TexData.h"

class Shader;

class ShaderPass {
public:
    ShaderPass(Shader* shader);
    ~ShaderPass();
    
    void update(ofTexture& inputTexture, TexData& texData);
    ofTexture& getTexture();
    bool isAllocated() const;
    void clear();
    const Shader& getShader() const {
        return *shader;
    }
    
private:
    Shader* shader;
    ofFbo fbo;
    ofTexture arrayTexture;  // For array texture output
    glm::vec3 outputSize;
    bool allocated;
    bool isArrayTexture;
};

