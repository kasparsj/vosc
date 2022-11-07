#include "ShaderPingPongTex.h"
#include "Layer.h"

void ShaderPingPongTex::update(Layer *layer, Texture* tex) {
    FboPingPongTex::update(layer, tex);
    
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
    
    fbo.dest()->begin();
    if (tex->fboSettings.numColorbuffers > 1) {
        fbo.dest()->activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
    }
    if (!tex->noClear) {
        ofClear(0, 0, 0, 0);
    }
    
    begin(layer);
    //glm::vec2 size = tex->getSize();
    fbo.source()->draw(0,0);
    end();
        
    fbo.dest()->end();
    fbo.swap();
    
    ofDisableBlendMode();
}

void ShaderPingPongTex::choose() {
    path = random();
}

void ShaderPingPongTex::reload() {
    cache.erase(path);
}
