#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxAssimpModelLoader.h"

class Layer;

struct BoundingBox {
    glm::vec3 min = glm::vec3(0,0,0);
    glm::vec3 max = glm::vec3(0,0,0);
};

class Geom {
public:
    static vector<string> primitives;
    static bool isPrimitive(const string& path);
    static string random();

    ~Geom() {}
    void load(const string& newPath, const vector<float>& args);
    void load(const ofxOscMessage &m);
    void load(string newPath) {
        vector<float> args;
        load(newPath, args);
    }
    void choose(const ofxOscMessage& m);
    void update();
    void oscCommand(const string& command, const ofxOscMessage& m);
    void draw() const;
    ofMesh& getMesh() { return *mesh; }
    glm::vec3 getSize() {
        return boundingBox.max - boundingBox.min;
    }
    glm::vec3 getScale(const glm::vec2& target) {
        if (path == "quad") {
            return glm::vec3(target, 1.f);
        }
        glm::vec3 size = getSize();
        return glm::vec3(MIN(target.x / size.x, target.y / size.y) * 0.9);
    }
    bool isLoaded() const {
        return path != "";
    }
    void set(const ofxOscMessage& m);
    
    bool drawWireframe;
    int drawInstanced = 0;

private:
    void updateBoundingBox();
    
    bool loadModel(const vector<float> &args);
    bool loadPrimitive(const vector<float>& args);
    bool loadQuad(const vector<float>& args);
    bool loadGrass(const vector<float>& args);
    
    string path = "";
    string prevPath = "";
    shared_ptr<of3dPrimitive> primitive = NULL;
    ofxAssimpModelLoader model;
    shared_ptr<ofMesh> mesh = NULL;
    BoundingBox boundingBox;
    bool usingModel;
};
