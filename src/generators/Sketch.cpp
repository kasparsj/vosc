#include "Sketch.h"
#include "NoisySpirals.h"
#include "WaveClock.h"

map<string, SketchImpl*> createSketches()
{
    map<string, SketchImpl*> sketches;
    sketches["NoisySpirals"] = new NoisySpirals();
    sketches["WaveClock"] = new WaveClock();
    return sketches;
}

map<string, SketchImpl*> Sketch::sketches = createSketches();

void Sketch::update(VisualData *data, Config &config) {
    if (name != prevName) {
        prevName = name;
        random = ofRandom(1000);
    }
    if (!sketches[name]->initialized) {
        sketches[name]->init();
    }
    if (!fbo.isAllocated() || (fbo.getWidth() != data->size.x || fbo.getHeight() != data->size.y)) {
        fbo.clear();
        fbo.allocate(data->size.x, data->size.y);
    }
    ofEnableAlphaBlending();
    fbo.begin();
    sketches[name]->draw(data, config);
    fbo.end();
    ofDisableAlphaBlending();
    if (data->mergedConfig.randomShader()) {
        choose();
    }
}

void Sketch::draw(int left, int top, int width, int height) {
    fbo.draw(left, top, width, height);
}

void Sketch::choose() {
    auto it = sketches.begin();
    advance(it, int(ofRandom(sketches.size())));
    name = it->first;
}

void Sketch::clear() {
    fbo.begin();
    ofClear(0, 0, 0, 0);
    fbo.end();
}

void Sketch::reset() {
    clear();
    sketches[name]->initialized = false;
}
