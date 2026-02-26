#include "Camera.hpp"
#include "Args.h"
#include "ofxFirstPersonCamera.h"

void Camera::setup() {
    if (!camPos || !camLook || !camOrbit) {
        reset();
    }
}

void Camera::reset() {
    camPos = VariablePool::createOrUpdateShared<glm::vec3>("camPos", glm::vec3(0, 0, -870));
    camLook = VariablePool::createOrUpdateShared<glm::vec3>("camLook", glm::vec3(0));
    camOrbit = VariablePool::createOrUpdateShared<float>("camOrbit", 0.f);
    orbit = 0.f;
    // todo: reset camera object
}

void Camera::update() {
    if (cam != nullptr) {
        ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam.get());
        ofxFirstPersonCamera* fpCam = dynamic_cast<ofxFirstPersonCamera*>(cam.get());
        if ((easyCam == nullptr && fpCam == nullptr) || !camPos->isConst()) {
            cam->setPosition(camPos->get());
        }
        if ((easyCam == nullptr && fpCam == nullptr) || !camLook->isConst()) {
            cam->lookAt(camLook->get());
        }
        if (camOrbit->get() != 0 && easyCam != nullptr) {
            orbit += ofGetLastFrameTime() * camOrbit->get();
            easyCam->orbitDeg(orbit, 0., easyCam->getDistance(), {0., 0., 0.});
        }
    }
}

void Camera::oscCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/cam") {
        oscInit(m);
    }
    else if (command == "/cam/reset") {
        reset();
    }
    else {
        if (cam == nullptr) {
            ofLog() << (command + " failed: camera not enabled (run /cam)");
        }
        else if (command == "/cam/pos") {
            if (m.getNumArgs() < 1) {
                ofLogError() << "/cam/pos expects at least one value: " << m;
                return;
            }
            camPos->set(m, 0);
            if (camPos->isConst()) {
                cam->setPosition(camPos->get());
            }
        }
        else if (command == "/cam/look") {
            if (m.getNumArgs() < 1) {
                ofLogError() << "/cam/look expects at least one value: " << m;
                return;
            }
            camLook->set(m, 0);
            if (camLook->isConst()) {
                cam->lookAt(camLook->get());
            }
        }
        else if (command == "/cam/orbit") {
            if (m.getNumArgs() < 1) {
                ofLogError() << "/cam/orbit expects one value: " << m;
                return;
            }
            VariablePool::createOrUpdateShared("camOrbit", "/var", m, 0);
            if (dynamic_cast<ofEasyCam*>(cam.get()) == nullptr) {
                ofLogError() << ("/cam/orbit supported only for 'easy' cam");
            }
        }
        else if (command == "/cam/set") {
            if (m.getNumArgs() < 1 || m.getArgType(0) != OFXOSC_TYPE_STRING) {
                ofLogError() << "/cam/set expects a string method name: " << m;
                return;
            }
            string method = m.getArgAsString(0);
            if (method == "nearClip" && m.getNumArgs() > 1) {
                cam->setNearClip(m.getArgAsFloat(1));
            }
            else if (method == "farClip" && m.getNumArgs() > 1) {
                cam->setFarClip(m.getArgAsFloat(1));
            }
            else if (method == "globalPosition" && m.getNumArgs() > 3) {
                cam->setGlobalPosition(m.getArgAsFloat(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
            }
            else {
                ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam.get());
                if (easyCam != nullptr) {
                    if (method == "distance" && m.getNumArgs() > 1) {
                        easyCam->setDistance(m.getArgAsFloat(1));
                    }
                    else if (method == "autoDistance" && m.getNumArgs() > 1) {
                        easyCam->setAutoDistance(m.getArgAsBool(1));
                    }
                    else if (method == "mouseInput" && m.getNumArgs() > 1) {
                        if (m.getArgAsBool(1)) {
                            easyCam->enableMouseInput();
                        }
                        else {
                            easyCam->disableMouseInput();
                        }
                    }
                }
                ofxFirstPersonCamera* fpCam = dynamic_cast<ofxFirstPersonCamera*>(cam.get());
                if (fpCam != nullptr) {
                    if (method == "movementMaxSpeed" && m.getNumArgs() > 1) {
                        fpCam->setMovementMaxSpeed(m.getArgAsFloat(1));
                    }
                }
            }
        }
    }
}

void Camera::oscInit(const ofxOscMessage &m) {
    string name = "";
    if (m.getNumArgs() > 0 && m.getArgType(0) == OFXOSC_TYPE_STRING) {
        name = m.getArgAsString(0);
    }
    if (name == "easy") {
        cam = std::unique_ptr<ofCamera>(new ofEasyCam());
    }
    else if (name == "fp") {
        cam = std::unique_ptr<ofCamera>(new ofxFirstPersonCamera());
    }
    else if (name != "") {
        cam = std::unique_ptr<ofCamera>(new ofCamera());
    }
    else {
        cam.reset();
    }
}
