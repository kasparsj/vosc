#pragma once

#include "ofMain.h"
#include "../tex/TexData.h"
#include <memory>

class Shader;

class ShaderPass {
public:
    explicit ShaderPass(const std::shared_ptr<Shader>& shader);
    ~ShaderPass();
    
    void update(ofTexture& inputTexture, TexData& texData);
    ofTexture& getTexture();
    bool isAllocated() const;
    void clear();
    std::shared_ptr<const Shader> getShader() const {
        return shader.lock();
    }
    
private:
    std::weak_ptr<Shader> shader;
    ofFbo fbo;
    ofTexture arrayTexture;  // For array texture output
    glm::vec3 outputSize;
    bool allocated;
    bool isArrayTexture;
};
