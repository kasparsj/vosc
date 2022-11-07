#pragma once

#include "Shader.h"
#include "FboPingPongTex.h"

class ShaderPingPongTex : public Shader, public FboPingPongTex {
    
public:
    ShaderPingPongTex(string name, const vector<float>& args) : FboPingPongTex(name, args) {}
    
    void update(Layer *layer, Texture* tex) override;
    void choose() override;
    void reload() override;
};
