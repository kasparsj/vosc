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
    static bool isPrimitive(string path);
    static string random();

    ~Geom() {
        if (primitive != NULL) {
            delete primitive;
        }
    }
    void load(string newPath, const vector<float>& args);
    void load(const ofxOscMessage &m);
    void load(string newPath) {
        vector<float> args;
        load(newPath, args);
    }
    void choose(const ofxOscMessage& m);
    void update();
    void draw() const;
    ofVboMesh& getMesh() { return mesh; }
    glm::vec3 getSize() {
        return boundingBox.max - boundingBox.min;
    }
    float getScale(const glm::vec2& target) {
        glm::vec3 size = getSize();
        return MIN(target.x / size.x, target.y / size.y) * 0.9;
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
    bool loadGrass(const vector<float>& args);
    
    string path = "";
    string prevPath = "";
    of3dPrimitive *primitive = NULL;
    ofxAssimpModelLoader model;
    ofVboMesh mesh;
    BoundingBox boundingBox;
    bool usingModel;
};
