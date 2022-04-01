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
    if (!fbo.isAllocated() || (fbo.getWidth() != data->size.x || fbo.getHeight() != data->size.y)) {
        fbo.clear();
        fbo.allocate(data->size.x, data->size.y);
    }
    if (name != prevName) {
        if (sketches.find(name) == sketches.end()) {
            ofLog() << "sketch " << name << " does not exist";
            name = prevName;
            return;
        }
        prevName = name;
    }
    if (!sketches[name]->initialized) {
        clear();
        sketches[name]->init();
        random = ofRandom(1000);
        config.color = sketches[name]->defaultColor;
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

void Sketch::reset() {
    sketches[name]->initialized = false;
}
