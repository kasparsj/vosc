#pragma once

#include "ofMain.h"
#include "VariablePool.h"
#include "ofxOsc.h"

class Camera {
public:
    static Camera& get() {
        if (instance == NULL) {
            Camera* cam = new Camera();
            cam->setup();
        }
        return (*instance);
    }
    
    void setup();
    void reset();
    void preUpdate() {
        ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
        if (easyCam != NULL) {
            camPos->set(cam->getPosition());
            camLook->set(cam->getLookAtDir());
        }
    }
    void update();
    bool isEnabled() {
        return cam != NULL;
    }
    glm::vec3 getPosition() {
        return cam->getPosition();
    }
    ofCamera& getCamera() {
        return *cam;
    }
    void oscCommand(const string& command, const ofxOscMessage& m);
    void oscInit(const ofxOscMessage& m);
    
private:
    static Camera* instance;
    
    ofCamera* cam;
    float orbit;
    
    shared_ptr<Variable<glm::vec3>> camPos;
    shared_ptr<Variable<glm::vec3>> camLook;
    shared_ptr<Variable<float>> camOrbit;
};
