#include "ShaderPingPongTex.h"
#include "Layer.h"
#include "Console.h"

void ShaderPingPongTex::update(TexData& data) {
    FboPingPongTex::update(data);
    
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
    
    fbo.dest()->begin();
    if (data.getFboSettings().numColorbuffers > 1) {
        fbo.dest()->activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
    }
    if (!data.noClear) {
        ofClear(0, 0, 0, 0);
    }
    
    begin(data);
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
