#include "LayerGeom.h"
#include "Layer.h"

vector<string> LayerGeom::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool LayerGeom::exists(string path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string LayerGeom::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

void LayerGeom::load(string newPath) {
    if (exists(newPath)) {
        if (primitive != NULL) {
            delete primitive;
        }
        primitive = NULL;
        prevPath = path;
        path = newPath;
    }
    else {
        ofLog() << "invalid geom " << newPath;
    }
}

void LayerGeom::choose() {
    path = random();
}

void LayerGeom::setShader(string path) {
    
}

void LayerGeom::update() {
    material.setup(matSettings);
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
    // needed for tex.bind() inside draw
    ofDisableArbTex();
}

void LayerGeom::draw() {
    if (layer->tex.hasTexture()) {
        const ofTexture& tex = layer->tex.getFbo().getTexture();
        tex.bind();
        _draw();
        tex.unbind();
    }
    else {
        _draw();
    }
    if (drawWireframe) {
        mesh.drawWireframe();
    }
}

void LayerGeom::_draw() {
    material.begin();
    if (drawInstanced > 1) {
        mesh.drawInstanced(OF_MESH_FILL, drawInstanced);
    }
    else {
        mesh.draw();
    }
    material.end();
}
