#include "Camera.hpp"
#include "Args.h"
#include "ofxFirstPersonCamera.h"

Camera* Camera::instance = NULL;

void Camera::setup() {
    if (instance == NULL) {
        reset();
        instance = this;
    }
}

void Camera::reset() {
    camPos = VariablePool::createOrUpdateShared<glm::vec3>("camPos", glm::vec3(0, 0, -870));
    camLook = VariablePool::createOrUpdateShared<glm::vec3>("camLook", glm::vec3(0));
    camOrbit = VariablePool::createOrUpdateShared<float>("camOrbit", 0.f);
    // todo: reset camera object
}

void Camera::update() {
    if (cam != NULL) {
        ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
        ofxFirstPersonCamera* fpCam = dynamic_cast<ofxFirstPersonCamera*>(cam);
        if ((easyCam == NULL && fpCam == NULL) || !camPos->isConst()) {
            cam->setPosition(camPos->get());
        }
        if ((easyCam == NULL && fpCam == NULL) || !camLook->isConst()) {
            cam->lookAt(camLook->get());
        }
        if (camOrbit->get() != 0 && easyCam != NULL) {
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
        if (cam == NULL) {
            ofLog() << (command + " failed: camera not enabled (run /cam)");
        }
        else if (command == "/cam/pos") {
            camPos->set(m, 0);
            if (camPos->isConst()) {
                cam->setPosition(camPos->get());
            }
        }
        else if (command == "/cam/look") {
            camLook->set(m, 0);
            if (camLook->isConst()) {
                cam->lookAt(camLook->get());
            }
        }
        else if (command == "/cam/orbit") {
            VariablePool::createOrUpdateShared("camOrbit", "/var", m, 0);
            if (dynamic_cast<ofEasyCam*>(cam) == NULL) {
                ofLogError() << ("/cam/orbit supported only for 'easy' cam");
            }
        }
        else if (command == "/cam/set") {
            string method = m.getArgAsString(0);
            if (method == "nearClip") {
                cam->setNearClip(m.getArgAsFloat(1));
            }
            else if (method == "farClip") {
                cam->setFarClip(m.getArgAsFloat(1));
            }
            else if (method == "globalPosition") {
                cam->setGlobalPosition(m.getArgAsFloat(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
            }
            else {
                ofEasyCam* easyCam = dynamic_cast<ofEasyCam*>(cam);
                if (easyCam != NULL) {
                    if (method == "distance") {
                        easyCam->setDistance(m.getArgAsFloat(1));
                    }
                    else if (method == "autoDistance") {
                        dynamic_cast<ofEasyCam*>(cam)->setAutoDistance(m.getArgAsBool(1));
                    }
                    else if (method == "mouseInput") {
                        if (m.getArgAsBool(1)) {
                            dynamic_cast<ofEasyCam*>(cam)->enableMouseInput();
                        }
                        else {
                            dynamic_cast<ofEasyCam*>(cam)->disableMouseInput();
                        }
                    }
                }
                ofxFirstPersonCamera* fpCam = dynamic_cast<ofxFirstPersonCamera*>(cam);
                if (fpCam != NULL) {
                    if (method == "movementMaxSpeed") {
                        fpCam->setMovementMaxSpeed(m.getArgAsFloat(1));
                    }
                }
            }
        }
    }
}

void Camera::oscInit(const ofxOscMessage &m) {
    string name = m.getArgAsString(0);
    if (name == "easy") {
        cam = new ofEasyCam();
    }
    else if (name == "fp") {
        cam = new ofxFirstPersonCamera();
    }
    else if (name != "") {
        cam = new ofCamera();
    }
    else {
        delete cam;
        cam = NULL;
    }
}
