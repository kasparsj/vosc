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

void SketchTex::update(TexData& data) {
    FboTex::update(data);
    if (path != prevPath) {
        if (!exists(path)) {
            ofLogError() << ("sketch does not exist: " + path);
            path = prevPath;
            return;
        }
        prevPath = path;
    }
    if (!sketches[path]->initialized) {
        clear();
        sketches[path]->init();
    }
    ofEnableBlendMode(data.blendMode);
    fbo.begin();
    sketches[path]->draw(data);
    fbo.end();
    ofDisableBlendMode();
}

void SketchTex::choose() {
    path = random();
}

void SketchTex::reset() {
    if (path != "") {
        sketches[path]->initialized = false;
    }
}
