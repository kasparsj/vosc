#include "SketchTex.h"
#include "Layer.h"
#include "NoisySpirals.h"
#include "WaveClock.h"
#include "Spiral.h"

map<string, Sketch*> createSketches()
{
    map<string, Sketch*> sketches;
    sketches["NoisySpirals"] = new NoisySpirals();
    sketches["WaveClock"] = new WaveClock();
    sketches["Spiral"] = new Spiral();
    return sketches;
}

map<string, Sketch*> SketchTex::sketches = createSketches();

bool SketchTex::exists(string path) {
    return sketches.find(path) != sketches.end();
}

string SketchTex::random() {
    auto it = sketches.begin();
    advance(it, int(ofRandom(sketches.size())));
    return it->first;
}

void SketchTex::update(Layer *layer, Texture* tex) {
    FboTex::update(layer, tex);
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
    ofEnableBlendMode(tex->blendMode);
    fbo.begin();
    sketches[path]->draw(layer);
    fbo.end();
    ofDisableBlendMode();
}

void SketchTex::draw(const glm::vec2 &size) {
    fbo.draw(0, 0, size.x, size.y);
}

void SketchTex::choose() {
    path = random();
}

void SketchTex::reset() {
    if (path != "") {
        sketches[path]->initialized = false;
    }
}
