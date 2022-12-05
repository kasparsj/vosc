#include "ShaderTex.h"
#include "Layer.h"
#include "Console.h"

void ShaderTex::update(TexData& data) {
    FboTex::update(data);
    if (!isLoaded()) {
        if (load(path)) {
            prevPath = path;
        }
        else {
            Console::get().error("could not load shader: " + path);
            path = prevPath;
            return;
        }
    }
    ofEnableBlendMode(data.blendMode);
    fbo.begin();
    if (data.getFboSettings().numColorbuffers > 1) {
        fbo.activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
    }
    if (!data.noClear) {
        ofClear(0, 0, 0, 0);
    }
    begin(data);
    ofDrawRectangle(0, 0, data.size.x, data.size.y);
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
