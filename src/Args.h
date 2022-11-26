#pragma once

#include "ofMain.h"
#include "ofxOscMessage.h"
#include "ofxEasing.h"

template <typename T>
struct Tween {
    T from;
    T to;
    float dur;
    float start;
    ofxeasing::function ease;
    function<void()> onComplete;
};

class Args {
public:
    static Args& getInstance() {
        return instance;
    }
    static void update() {
        Args& args = Args::getInstance();
        args.updateFloats();
        args.updateVecs();
        args.updateColors();
    }
    
    float parseIntOrFloat(const ofxOscMessage &m, int i = 0);
    ofFloatColor parseColor(const ofxOscMessage& m, int idx = 0);
    ofFloatColor parseLerpColor(const ofxOscMessage &m, int firstArg = 1);
    vector<float> parseAlign(const ofxOscMessage &m, int firstArg = 1);
    void handleFloat(float* value, const ofxOscMessage& m, int firstArg = 1);
    void handlePercent(float* value, const ofxOscMessage& m, int firstArg = 1);
    void handleVec3(glm::vec3* value, const ofxOscMessage& m, int firstArg = 1);
    void tweenVec3(glm::vec3* value, const ofxOscMessage& m, int firstArg = 1);
    void tweenVec3(glm::vec3* value, const ofxOscMessage& m, int firstArg, function<void()> onComplete);
    void handleVec3(vector<float>* value, const ofxOscMessage& m, int firstArg = 1);
    void handleColor(ofFloatColor* value, const ofxOscMessage& m, int firstArg = 1);
    void handleColor(vector<float>* value, const ofxOscMessage& m, int firstArg = 1);
    bool isTweenVec3(const ofxOscMessage& m, int firstArg = 1);
    void createTween(float* value, float target, float dur, function<void()> onComplete, ofxeasing::function ease);
    void createTween(float* value, float target, float dur, function<void()> onComplete, string ease) {
        // todo: implement
        createTween(value, target, dur, onComplete, ofxeasing::linear::easeNone);
    }
    void createTween(float* value, float target, float dur, string ease) {
        // todo: implement
        createTween(value, target, dur, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(float* value, float &target, float dur, ofxeasing::function ease) {
        createTween(value, target, dur, function<void()>(), ease);
    }
    void createTween(float* value, float target, float dur) {
        createTween(value, target, dur, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(float* value, float target) {
        createTween(value, target, 1.0, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3* value, const glm::vec3 &target, float dur, function<void()> onComplete, ofxeasing::function ease);
    void createTween(glm::vec3* value, const glm::vec3 &target, float dur, function<void()> onComplete, string ease) {
        // todo: implement
        createTween(value, target, dur, onComplete, ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3* value, const glm::vec3 &target, float dur, function<void()> onComplete) {
        createTween(value, target, dur, onComplete, ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3* value, const glm::vec3 &target, float dur) {
        createTween(value, target, dur, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(glm::vec3* value, const glm::vec3 &target) {
        createTween(value, target, 1.0, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(ofFloatColor* value, const ofFloatColor &target, float dur, function<void()> onComplete, ofxeasing::function ease);
    void createTween(ofFloatColor* value, const ofFloatColor &target, float dur, function<void()> onComplete, string ease) {
        // todo: implement
        createTween(value, target, dur, onComplete, ofxeasing::linear::easeNone);
    }
    void createTween(ofFloatColor* value, const ofFloatColor &target, float dur) {
        createTween(value, target, dur, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(ofFloatColor* value, const ofFloatColor &target) {
        createTween(value, target, 1.0, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(vector<float>* value, const vector<float> &target, float dur, function<void()> onComplete, ofxeasing::function ease);
    void createTween(vector<float>* value, const vector<float> &target, float dur, function<void()> onComplete, string ease) {
        // todo: implement
        createTween(value, target, dur, onComplete, ofxeasing::linear::easeNone);
    }
    void createTween(vector<float>* value, const vector<float> &target, float dur) {
        createTween(value, target, dur, function<void()>(), ofxeasing::linear::easeNone);
    }
    void createTween(vector<float>* value, const vector<float> &target) {
        createTween(value, target, 1.0, function<void()>(), ofxeasing::linear::easeNone);
    }
    void updateFloats();
    void updateVecs();
    void updateColors();
    
private:
    static Args instance;
    
    map<float*, Tween<float>> floatTweens;
    map<glm::vec3*, Tween<glm::vec3>> vec3Tweens;
    map<ofFloatColor*, Tween<ofFloatColor>> colorTweens;
};
