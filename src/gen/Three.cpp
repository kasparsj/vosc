#include "Three.h"
#include "Layer.h"

vector<string> Three::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool Three::exists(string path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string Three::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

void Three::update(Layer *layer) {
    if (primitive == NULL) {
        if (path == "box") {
            primitive = new ofBoxPrimitive();
        }
        else if (path == "sphere") {
            primitive = new ofSpherePrimitive();
        }
        else if (path == "icosphere") {
            primitive = new ofIcoSpherePrimitive();
        }
        else if (path == "cylinder") {
            primitive = new ofCylinderPrimitive();
        }
        else if (path == "plane") {
            primitive = new ofPlanePrimitive();
        }
        else if (path == "cone") {
            primitive = new ofConePrimitive();
        }
        if (primitive == NULL) {
            ofLog() << "could not load image: " << path;
            path = prevPath;
            return;
        }
        prevPath = path;
        layer->randomSeed = ofRandom(1000);
        if (layer->color == ofFloatColor(0, 0)) {
            layer->color = ofFloatColor(1);
        }
    }
    color = layer->getColor();
}

void Three::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    primitive->setPosition(pos);
    //primitive->setScale(size);
    primitive->draw();
}

void Three::choose() {
    path = random();
}

ofFloatColor Three::getTint(Layer *layer) {
    return layer->getColor();
}
