#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

class Layer;

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
    void choose();
    void update();
    void draw();
    ofVboMesh& getMesh() { return mesh; }
    bool isLoaded() const {
        return path != "";
    }
    void set(const ofxOscMessage& m);
    
    bool drawWireframe;
    int drawInstanced = 0;

private:
    bool loadPrimitive(const vector<float>& args);
    bool loadGrass(const vector<float>& args);
    
    string path = "";
    string prevPath = "";
    of3dPrimitive *primitive = NULL;
    ofVboMesh mesh;
};
