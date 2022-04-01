#include "Sketch.h"
#include "NoisySpirals.h"

map<string, SketchImpl*> createSketches()
{
    map<string, SketchImpl*> sketches;
    sketches["NoisySpirals"] = new NoisySpirals();
    return sketches;
}

map<string, SketchImpl*> Sketch::sketches = createSketches();

void Sketch::update(ShaderData *shaderData, Config &config) {
    if (name != prevName) {
        prevName = name;
        random = ofRandom(1000);
    }
    if (!sketches[name]->initialized) {
        sketches[name]->init();
    }
    if (!fbo.isAllocated() || (fbo.getWidth() != shaderData->size.x || fbo.getHeight() != shaderData->size.y)) {
        fbo.clear();
        fbo.allocate(shaderData->size.x, shaderData->size.y);
    }
    ofEnableAlphaBlending();
    fbo.begin();
    sketches[name]->draw(shaderData, config);
    fbo.end();
    ofDisableAlphaBlending();
    if (shaderData->mergedConfig.randomShader()) {
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
