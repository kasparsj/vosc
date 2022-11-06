#include "LayerGeom.h"
#include "Layer.h"

vector<string> LayerGeom::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool LayerGeom::isPrimitive(string path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string LayerGeom::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

void LayerGeom::load(string newPath, const vector<float>& args) {
    if (isPrimitive(newPath)) {
        if (primitive != NULL) {
            delete primitive;
        }
        primitive = NULL;
        prevPath = path;
        path = newPath;
        loadPrimitive(args);
        if (primitive == NULL) {
            ofLog() << "could not load geom: " << path;
            path = prevPath;
            return;
        }
        prevPath = path;
    }
    else {
        ofLog() << "invalid geom " << newPath;
    }
}

void LayerGeom::load(const ofxOscMessage &m) {
    string newPath = m.getArgAsString(1);
    vector<float> args;
    for (int i=2; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    load(newPath, args);
}

void LayerGeom::loadPrimitive(const vector<float>& args) {
    if (path == "plane") {
        primitive = new ofPlanePrimitive(args.size() > 0 ? args[0] : 100,
                                         args.size() > 1 ? args[1] : 100,
                                         args.size() > 2 ? args[2] : 2,
                                         args.size() > 3 ? args[3] : 2);
    }
    else if (path == "box") {
        primitive = new ofBoxPrimitive(args.size() > 0 ? args[0] : 100,
                                       args.size() > 1 ? args[1] : 100,
                                       args.size() > 2 ? args[2] : 100);
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
}

void LayerGeom::choose() {
    path = random();
}

void LayerGeom::update() {
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
    if (drawInstanced > 1) {
        mesh.drawInstanced(OF_MESH_FILL, drawInstanced);
    }
    else {
        mesh.draw();
    }
}
