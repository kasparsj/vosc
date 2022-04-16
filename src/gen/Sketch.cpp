#include "Sketch.h"
#include "Layer.h"
#include "NoisySpirals.h"
#include "WaveClock.h"
#include "Spiral.h"

map<string, SketchImpl*> createSketches()
{
    map<string, SketchImpl*> sketches;
    sketches["NoisySpirals"] = new NoisySpirals();
    sketches["WaveClock"] = new WaveClock();
    sketches["Spiral"] = new Spiral();
    return sketches;
}

map<string, SketchImpl*> Sketch::sketches = createSketches();

bool Sketch::exists(string path) {
    return sketches.find(path) != sketches.end();
}

string Sketch::random() {
    auto it = sketches.begin();
    advance(it, int(ofRandom(sketches.size())));
    return it->first;
}

void Sketch::update(Layer *layer) {
    if (!fbo.isAllocated() || (fbo.getWidth() != layer->size.x || fbo.getHeight() != layer->size.y)) {
        fbo.clear();
        fbo.allocate(layer->size.x, layer->size.y);
    }
    if (path != prevPath) {
        if (!exists(path)) {
            ofLog() << "sketch " << path << " does not exist";
            path = prevPath;
            return;
        }
        prevPath = path;
        layer->randomSeed = ofRandom(1000);
        if (layer->color == ofFloatColor(0, 0)) {
            layer->color = sketches[path]->defaultColor;
        }
    }
    if (!sketches[path]->initialized) {
        clear();
        sketches[path]->init();
    }
    ofEnableAlphaBlending();
    fbo.begin();
    sketches[path]->draw(layer);
    fbo.end();
    ofDisableAlphaBlending();
    if (layer->randomShader()) {
        choose();
    }
}

void Sketch::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    fbo.draw(pos, size.x, size.y);
}

void Sketch::choose() {
    path = random();
}

void Sketch::reset() {
    if (path != "") {
        sketches[path]->initialized = false;
    }
}
