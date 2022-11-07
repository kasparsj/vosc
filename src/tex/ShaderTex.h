#pragma once

#include "Shader.h"
#include "FboTex.h"

class ShaderTex : public Shader, public FboTex {
    
public:
    ShaderTex(string name, const vector<float>& args) : FboTex(name, args) {}
    
    void update(Layer *layer, Texture* tex) override;
    void choose() override;
    void reload() override;
};
