#pragma once

#include "Shader.h"
#include "FBOTex.h"

class ShaderTex : public Shader, public FBOTex {
    
public:
    ShaderTex(string name, const vector<float>& args) : FBOTex(name, args) {}
    
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    void reload() override;
};
