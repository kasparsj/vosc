#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxAssimpModelLoader.h"
#include "VarsHolder.h"

class Layer;

struct BoundingBox {
    glm::vec3 min = glm::vec3(0,0,0);
    glm::vec3 max = glm::vec3(0,0,0);
};

class Geom : public VarsHolder {
public:
    static vector<string> primitives;
    static bool isPrimitive(const string& path);
    static string random();

    ~Geom() {}
    void load(const string& newPath, const ofxOscMessage& m, int idx = 1);
    void load(const ofxOscMessage &m, int idx = 1);
    void load(string newPath) {
        ofxOscMessage m;
        load(newPath, m, 1);
    }
    void choose(const ofxOscMessage& m);
    void update();
    void oscCommand(const string& command, const ofxOscMessage& m);
    void draw() const;
    bool hasMesh() { return mesh != NULL; }
    ofMesh& getMesh() { return *mesh; }
    glm::vec3 getSize() {
        return boundingBox.max - boundingBox.min;
    }
    glm::vec3 getScale(const glm::vec2& target) {
        if (path == "quad") {
            return glm::vec3(target / 2.0, 1.f);
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
    
    bool loadModel(const ofxOscMessage& m, int idx = 1);
    bool loadPrimitive(const ofxOscMessage& m, int idx = 1);
    bool loadQuad(const ofxOscMessage& m, int idx = 1);
    bool loadGrass(const ofxOscMessage& m, int idx = 1);
    void appendMesh(ofMesh mesh, const glm::mat4 mat);
    bool appendPrimitive(const shared_ptr<of3dPrimitive>& primitive);
    bool appendPrimitive(const shared_ptr<BaseVar>& var, int i);
    shared_ptr<of3dPrimitive> createPrimitive();
    shared_ptr<of3dPrimitive> createPrimitive(const vector<float>& args);
    
    string path = "";
    string prevPath = "";
    mutable ofxAssimpModelLoader model;
    shared_ptr<ofMesh> mesh;
    BoundingBox boundingBox;
    bool usingModel;
};
