#pragma once

#include "ofMain.h"
#include "VariablePool.h"
#include "ofxOsc.h"
#include <memory>
#ifdef VOSC_RECOVERY_TESTING
#include <array>
#endif

class Camera {
public:
    void setup();
    void reset();
    void resetScene();
    void preUpdate() {
        ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam.get());
        if (easyCam != nullptr) {
            camPos->set(cam->getPosition());
            camLook->set(cam->getLookAtDir());
        }
    }
    void update();
    bool isEnabled() const {
        return cam != nullptr;
    }
    glm::vec3 getPosition() {
        if (!cam) {
            return glm::vec3(0.f);
        }
        return cam->getPosition();
    }
    ofCamera& getCamera() {
        return *cam;
    }
    const ofCamera& getCamera() const {
        return *cam;
    }
    void oscCommand(const string& command, const ofxOscMessage& m);
    void oscInit(const ofxOscMessage& m);
#ifdef VOSC_RECOVERY_TESTING
    std::array<const BaseVar*, 3> recoveryTestManagedVariables() const {
        return {{camPos.get(), camLook.get(), camOrbit.get()}};
    }
#endif
    
private:
    std::unique_ptr<ofCamera> cam;
    float orbit = 0.f;
    
    shared_ptr<Variable<glm::vec3>> camPos;
    shared_ptr<Variable<glm::vec3>> camLook;
    shared_ptr<Variable<float>> camOrbit;
};
