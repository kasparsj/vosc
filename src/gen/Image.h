#pragma once

#include "Gen.h"

class Image : public Gen {
public:
    static vector<string> cache;
    static string random();
    
    Image(string path) : Gen(path) {}
    void update(Layer *layer) override;
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    
private:
    ofImage image;
};
