#pragma once

#include "Tex.h"

class ImageTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    
    ImageTex(string path, const vector<float>& args) : Tex(path, args) {}
    ~ImageTex() {}
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofPixels& getPixels() override {
        return image.getPixels();
    }
    
private:
    ofImage image;
};
