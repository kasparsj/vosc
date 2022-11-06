#pragma once

#include "Tex.h"

class ColorTex : public Tex {
public:
    static bool exists(string path);
    static string random();
    
    ColorTex(string path) : Tex(path) {}
    ~ColorTex() {}
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofPixels& getPixels() override {
        return pixels;
    }
    
private:
    ofColor color;
    ofPixels pixels;
};
