#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

class Layer;

class LayerGeom {
public:
    static vector<string> primitives;
    static bool isPrimitive(string path);
    static string random();

    ~LayerGeom() {
        if (primitive != NULL) {
            delete primitive;
        }
    }
    void setup(Layer* layer) {
        this->layer = layer;
        vector<float> args;
        load("plane", args);
    }
    void load(string newPath, const vector<float>& args);
    void load(const ofxOscMessage &m);
    void choose();
    void update();
    void draw();
    ofVboMesh& getMesh() { return mesh; }
    
    bool drawWireframe;
    int drawInstanced = 0;

private:
    bool loadPrimitive(const vector<float>& args);
    bool loadGrass(const vector<float>& args);
    
    Layer* layer;
    string path = "";
    string prevPath = "";
    of3dPrimitive *primitive = NULL;
    ofVboMesh mesh;
};
