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

string Sketch::random() {
    auto it = sketches.begin();
    advance(it, int(ofRandom(sketches.size())));
    return it->first;
}

void Sketch::update(Layer *layer, const Config &config) {
    if (!fbo.isAllocated() || (fbo.getWidth() != layer->size.x || fbo.getHeight() != layer->size.y)) {
        fbo.clear();
        fbo.allocate(layer->size.x, layer->size.y);
    }
    if (name != prevName) {
        if (sketches.find(name) == sketches.end()) {
            ofLog() << "sketch " << name << " does not exist";
            name = prevName;
            return;
        }
        prevName = name;
        layer->randomSeed = ofRandom(1000);
        if (layer->color == ofFloatColor(0, 0)) {
            layer->color = sketches[name]->defaultColor;
        }
    }
    if (!sketches[name]->initialized) {
        clear();
        sketches[name]->init();
    }
    ofEnableAlphaBlending();
    fbo.begin();
    sketches[name]->draw(layer, config);
    fbo.end();
    ofDisableAlphaBlending();
    if (layer->randomShader()) {
        choose();
    }
}

void Sketch::draw(int left, int top, int width, int height) {
    fbo.draw(left, top, width, height);
}

void Sketch::choose() {
    name = random();
}

void Sketch::reset() {
    if (name != "") {
        sketches[name]->initialized = false;
    }
}
