#pragma once

#include "Gen.h"

class Image : public Gen {
public:
    static vector<string> cache;
    static string random();
    
    Image(string path) : Gen(path) {}
    ~Image() {}
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofPixels& getPixels() override {
        return image.getPixels();
    }
    
private:
    ofImage image;
};
