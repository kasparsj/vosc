#pragma once

#include "Gen.h"

class Video : public Gen {
public:
    static vector<string> videos;
    static string random();
    
    Video(string name) : Gen(name) {}
    void update(Layer *layer) override;
    void seek(float pos);
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    
private:
    ofVideoPlayer videoPlayer;
};
