#pragma once

#include "ofMain.h"

class Layer;

class LayerGeom {
public:
    static vector<string> primitives;
    static bool exists(string path);
    static string random();

    ~LayerGeom() {
        if (primitive != NULL) {
            delete primitive;
        }
    }
    void setup(Layer* layer) {
        this->layer = layer;
        load("plane");
    }
    void load(string newPath);
    void choose();
    void setShader(string path);
    void update();
    void draw();
    void _draw();
    const ofVboMesh& getMesh() const { return mesh; }
    
    bool drawWireframe;
    int drawInstanced = 0;
    ofMaterialSettings matSettings;

private:
    Layer* layer;
    string path = "";
    string prevPath = "";
    of3dPrimitive *primitive = NULL;
    ofVboMesh mesh;
    ofMaterial material;
};
