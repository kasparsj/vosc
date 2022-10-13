#pragma once

#include "Gen.h"

class Webcam : public Gen {
public:
    static bool exists(int device);
    static int random();
    
    Webcam(string path) : Gen(path) {}
    ~Webcam() {}
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofFloatColor getTint(Layer *layer) override;

private:
    ofVideoGrabber vidGrabber;
};
