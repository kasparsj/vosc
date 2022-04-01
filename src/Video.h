#pragma once

#include "Source.h"

class Video : public Source {
public:
    static vector<string> videos;
    
    void update(const Config &config) override;
    void resetPos();
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    
    float pos = 0;
    
private:
    ofVideoPlayer videoPlayer;
};
