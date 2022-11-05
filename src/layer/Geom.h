#pragma once

#include "ofMain.h"

class Geom {
public:
    static vector<string> primitives;
    static bool exists(string path);
    static string random();

    Geom(string path);
    Geom() { set("plane"); }
    ~Geom() {
        if (primitive != NULL) {
            delete primitive;
        }
    }
    void set(string newPath) {
        if (primitive != NULL) {
            delete primitive;
        }
        primitive = NULL;
        prevPath = path;
        path = newPath;
    }
    void update();
    void choose();
    const ofVboMesh& getMesh() const { return mesh; }

private:
    string path = "";
    string prevPath = "";
    of3dPrimitive *primitive = NULL;
    ofVboMesh mesh;
};
