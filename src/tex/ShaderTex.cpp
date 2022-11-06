#include "ShaderTex.h"
#include "Layer.h"

void ShaderTex::update(Layer *layer) {
    if (!fbo.isAllocated() || (fbo.getWidth() != layer->size.x || fbo.getHeight() != layer->size.y)) {
        fbo.clear();
        fbo.allocate(layer->size.x, layer->size.y);
    }
    if (!isLoaded()) {
        if (load(path)) {
            prevPath = path;
            layer->randomSeed = ofRandom(1000);
        }
        else {
            ofLog() << "could not load shader: " << path;
            path = prevPath;
            return;
        }
    }
    ofEnableAlphaBlending();
    fbo.begin();
    if (!layer->noClear) {
        ofClear(0, 0, 0, 0);
    }
    if (layer->data.visible) {
        begin(layer);
        ofDrawRectangle(0, 0, layer->size.x, layer->size.y);
        end();
    }
    fbo.end();
    ofDisableAlphaBlending();
    if (layer->randomShader()) {
        choose();
    }
}

void ShaderTex::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    fbo.draw(pos, size.x, size.y);
}

void ShaderTex::choose() {
    path = random();
}

void ShaderTex::reload() {
    cache.erase(path);
}
