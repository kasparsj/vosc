#pragma once

#include "Tex.h"

class WebcamTex : public Tex {
public:
    static bool exists(int device);
    static int random();
    
    WebcamTex(string path) : Tex(path) {}
    ~WebcamTex() {
        vidGrabber.close();
    }
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    bool isFrameNew() override {
        return vidGrabber.isFrameNew();
    }
    ofPixels& getPixels() override {
        return vidGrabber.getPixels();
    }

private:
    ofVideoGrabber vidGrabber;
};
