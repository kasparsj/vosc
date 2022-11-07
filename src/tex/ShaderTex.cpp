#include "ShaderTex.h"
#include "Layer.h"

void ShaderTex::update(Layer *layer, Texture* tex) {
    FBOTex1::update(layer, tex);
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
    ofEnableBlendMode(tex->blendMode);
    fbo.begin();
    if (tex->fboSettings.numColorbuffers > 1) {
        fbo.activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
    }
    if (!tex->noClear) {
        ofClear(0, 0, 0, 0);
    }
    glm::vec2 size = tex->getSize();
    begin(layer);
    ofDrawRectangle(0, 0, size.x, size.y);
    end();
    fbo.end();
    ofDisableBlendMode();
}

void ShaderTex::choose() {
    path = random();
}

void ShaderTex::reload() {
    cache.erase(path);
}
