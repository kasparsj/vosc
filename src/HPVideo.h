#pragma once

#include "Gen.h"
#include "ofxHPVPlayer.h"

class HPVideo : public Gen {
public:
    static vector<string> cache;
    static string random();
    static bool engineInitialized;
    
    HPVideo(string name) : Gen(name) {
        if (!engineInitialized) {
            HPV::InitHPVEngine();
        }
        hpvPlayer.init(HPV::NewPlayer());
    }
    ~HPVideo() {}
    void update(Layer *layer) override;
    void seek(float pct);
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofFloatColor getTint(Layer *layer) override;
    
private:
    ofxHPVPlayer hpvPlayer;
};
