#include "SketchTex.h"
#include "Layer.h"
#include "NoisySpirals.h"
#include "WaveClock.h"
#include "Spiral.h"

map<string, shared_ptr<Sketch>> createSketches()
{
    map<string, shared_ptr<Sketch>> sketches;
    sketches["NoisySpirals"] = make_shared<NoisySpirals>();
    sketches["WaveClock"] = make_shared<WaveClock>();
    sketches["Spiral"] = make_shared<Spiral>();
    return sketches;
}

map<string, shared_ptr<Sketch>> SketchTex::sketches = createSketches();

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
