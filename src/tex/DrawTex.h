#pragma once

#include "Tex.h"

class DrawTex : public Tex {
public:
    static vector<string> draws;
    static string random();
    
    DrawTex(string path, const vector<float>& args) : Tex(path, args) {}
    ~DrawTex() {}
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofPixels& getPixels() override {
        return pixels;
    }
    
private:
    ofPixels pixels;

};
