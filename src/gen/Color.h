#pragma once

#include "Gen.h"

class Color : public Gen {
public:
    static bool exists(string path);
    static string random();
    
    Color(string path) : Gen(path) {}
    ~Color() {}
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
