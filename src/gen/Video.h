#pragma once

#include "Gen.h"

class Video : public Gen {
public:
    static vector<string> videos;
    
    void update(VisualData *data, Config &config) override;
    void resetPos();
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    
    float pos = 0;
    
private:
    ofVideoPlayer videoPlayer;
};
