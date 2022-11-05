#include "Geom.h"

vector<string> Geom::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool Geom::exists(string path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string Geom::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

Geom::Geom(string path) {
    set(path);
}

void Geom::update() {
    if (primitive == NULL) {
        if (path == "plane") {
            primitive = new ofPlanePrimitive(100, 100, 2, 2);
        }
        else if (path == "box") {
            primitive = new ofBoxPrimitive(100, 100, 100);
        }
        else if (path == "sphere") {
            primitive = new ofSpherePrimitive(50, 16);
        }
        else if (path == "icosphere") {
            primitive = new ofIcoSpherePrimitive(50, 2);
        }
        else if (path == "cylinder") {
            primitive = new ofCylinderPrimitive();
        }
        else if (path == "cone") {
            primitive = new ofConePrimitive();
        }
        if (primitive == NULL) {
            ofLog() << "could not load geom: " << path;
            path = prevPath;
            return;
        }
        prevPath = path;
    }
    if (primitive != NULL) {
        //primitive->setPosition(pos);
        //primitive->setScale(size);
        //primitive->draw();
        mesh = primitive->getMesh();
    }
}

void Geom::choose() {
    path = random();
}
