#pragma once

#include "ofMain.h"
#include "TexData.h"

class ShaderPass {
public:
    ShaderPass();
    ~ShaderPass();
    
    void setup(const string& shaderPath, float width = -1, float height = -1, float depth = -1);
    void update(ofTexture& inputTexture, TexData& texData);
    ofTexture& getTexture();
    bool isAllocated() const;
    void clear();
    
private:
    string shaderPath;
    ofShader shader;
    ofFbo fbo;
    ofTexture arrayTexture;  // For array texture output
    glm::vec3 outputSize;
    bool widthSet;
    bool heightSet;
    bool depthSet;
    bool allocated;
    bool isArrayTexture;
};

