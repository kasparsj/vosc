#pragma once

#include "Shader.h"
#include "FBOTex1.h"

class ShaderTex : public Shader, public FBOTex1 {
    
public:
    ShaderTex(string name, const vector<float>& args) : FBOTex1(name, args) {}
    
    void update(Layer *layer, Texture* tex) override;
    void choose() override;
    void reload() override;
};
