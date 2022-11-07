#include "Texture.h"
#include "ShaderTex.h"
#include "VideoTex.h"
#include "SketchTex.h"
#include "ImageTex.h"
#include "HPVideoTex.h"
#include "WebcamTex.h"
#include "DrawTex.h"

bool isColor(string path) {
    return (path.substr(0, 1) == "#" && path.size() == 7) || (path.substr(0, 2) == "0x" && path.size() == 8);
}

Tex* Texture::factory(string type, string path, const vector<float>& args) {
    Tex *tex = NULL;
    auto it = SourceMap.find(type);
    if (it != SourceMap.end()) {
        switch (it->second) {
            case Source::VIDEO:
                tex = new VideoTex(path, args);
                break;
            case Source::HPV:
                tex = new HPVideoTex(path, args);
                break;
            case Source::SHADER:
                tex = new ShaderTex(path, args);
                break;
            case Source::SKETCH:
                tex = new SketchTex(path, args);
                break;
            case Source::IMAGE:
                tex = new ImageTex(path, args);
                break;
            case Source::WEBCAM:
                tex = new WebcamTex(path, args);
                break;
            case Source::DRAW:
                tex = new DrawTex(path, args);
                break;
            case Source::COLOR:
                tex = new ShaderTex("color", args);
                dynamic_cast<ShaderTex*>(tex)->setUniform("color", args);
                break;
        }
    }
    return tex;
}

Tex* Texture::factory(string source, const vector<float>& args) {
    string type = source;
    string path = "";
    bool explicitType = source.find(":") != string::npos;
    if (explicitType) {
        type = source.substr(0, source.find(":"));
        path = source.substr(source.find(":") + 1);
    }
    if (path == "") {
        auto it = SourceMap.find(type);
        if (it != SourceMap.end()) {
            switch (it->second) {
                case Source::VIDEO:
                    path = VideoTex::random();
                    break;
                case Source::HPV:
                    path = HPVideoTex::random();
                    break;
                case Source::SHADER:
                    path = Shader::random();
                    break;
                case Source::SKETCH:
                    path = SketchTex::random();
                    break;
                case Source::IMAGE:
                    path = ImageTex::random();
                    break;
                case Source::WEBCAM:
                    path = WebcamTex::random();
                    break;
                case Source::DRAW:
                    path = DrawTex::random();
                    break;
                case Source::COLOR: {
                    Tex* tex = factory("shader", "color", args);
                    ofFloatColor color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
                    dynamic_cast<ShaderTex*>(tex)->setUniform("color", color);
                    return tex;
                }
            }
        }
    }
    return factory(type, path, args);
}

void Texture::load(string source, const vector<float>& args) {
    unload();
    bool explicitType = source.find(":") != string::npos;
    if (explicitType && source.substr(0, 4) != "http") {
        tex = factory(source, args);
    }
    else {
        string extension = ofFile(source).getExtension();
        for (int i=0; i<extension.size(); i++) {
            extension[i] = tolower(extension[i]);
        }
        if (extension == "frag") {
            tex = factory("shader", source, args);
        }
        else if (extension == "jpg" || extension == "jpeg" || extension == "png") {
            tex = factory("image", source, args);
        }
        else if (extension == "mov") {
            tex = factory("video", source, args);
        }
        else if (extension == "hpv") {
            tex = factory("hpv", source, args);
        }
        else {
            if (isColor(source)) {
                ofFloatColor color = ofFloatColor::fromHex(ofHexToInt(source));
                vector<float> args1 = {color.r, color.g, color.b};
                tex = factory("shader", "color", args1);
            }
            else if (SketchTex::exists(source)) {
                tex = factory("sketch", source, args);
            }
        }
    }
    if (tex == NULL) {
        ofLog() << "invalid source " << source;
    }
}

void Texture::load(const ofxOscMessage &m, int arg) {
    string newPath = m.getArgAsString(arg);
    vector<float> args;
    for (int i=(arg+1); i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    load(newPath, args);
}

void Texture::choose(string type, const vector<float>& args) {
    if (type == "") {
        auto it = SourceMap.begin();
        advance(it, int(ofRandom(SourceMap.size())));
        type = it->first;
    }
    tex = factory(type, args);
    if (tex == NULL) {
        ofLog() << "invalid source type " << type;
    }
}

void Texture::choose(const ofxOscMessage& m) {
    string type = m.getNumArgs() > 1 ? m.getArgAsString(1) : "";
    vector<float> args;
    for (int i=2; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    choose(type, args);
}

void Texture::unload() {
    frames.clear();
    frames.resize(numFrames);
    if (tex != NULL) {
        delete tex;
        tex = NULL;
    }
}

void Texture::reload() {
    if (tex != NULL) {
        tex->reload();
    }
    else {
        ofLog() << "cannot reload layer " << index;
    }
}

void Texture::clear() {
    if (tex != NULL) {
        tex->clear();
    }
    else {
        ofLog() << "cannot clear layer " << index;
    }
}

void Texture::update(Layer* layer) {
    if (isLoaded()) {
        if (fboSettings.width == 0) {
            fboSettings.width = layer->size.x;
        }
        if (fboSettings.height == 0) {
            fboSettings.height = layer->size.y;
        }
        tex->update(layer, this);
        
        if (looper != NULL) {
            looper->swapBuffers(/*forceSwap*/);
            if (tex->isFrameNew()){
                looper->addFrame(tex->getPixels());
            }
            looper->update();
        }
        
        if (numFrames > 1) {
            drawFrame();
        }
    }
}

void Texture::drawFrame() {
    curFbo = (curFbo + 1) % frames.size();
    ofFbo& fbo = frames[curFbo];
    if (!fbo.isAllocated()) {
        // todo: should frame fboSettings be different or same?
        allocate(fbo);
    }
    fbo.begin();
    ofClear(0, 0, 0, 0);
    glm::vec2 size = getSize();
    if (looper == NULL) {
        tex->draw(size);
    }
    else {
        ofSetColor(255);
        looper->draw(0, 0, size.x, size.y);
    }
    fbo.end();
}

void Texture::allocate(ofFbo& fbo) {
    ofDisableTextureEdgeHack();
    fbo.allocate(fboSettings);
    ofEnableTextureEdgeHack();
}

void Texture::reset() {
    tex->reset();
}

const ofFbo& Texture::getFrame(int delay) const {
    int i = curFbo - delay;
    while (i<0) i += frames.size();
    return frames[MIN(frames.size()-1, i)];
}

bool Texture::hasTexture(int delay) const {
    if (curFbo < 0) return false;
    const ofFbo& fbo = getFrame(delay);
    return fbo.isAllocated() && fbo.getTexture().isAllocated();
}

const ofTexture& Texture::getTexture(int delay) const {
    if (numFrames <= 1) {
        if (looper == NULL) {
            return tex->getTexture();
        }
        else {
            return looper->getFbo().getTexture();
        }
    }
    return getFrame(delay).getTexture();
}

ofPixels& Texture::getPixels() const {
    return tex->getPixels();
}

void Texture::setLooper(const ofxOscMessage& m) {
    if (m.getArgAsFloat(1) == 0) {
        delete looper;
        looper = NULL;
    }
    else {
        float maxDuration = m.getArgAsFloat(1);
        int fps = m.getNumArgs() > 2 ? m.getArgAsInt(2) : 30;
        int speed = m.getNumArgs() > 3 ? m.getArgAsInt(3) : 2.0;
        if (looper == NULL) {
            looper = new ofxLooper();
            looper->setup(maxDuration, fps, speed);
        }
        else {
            looper->setMaxDuration(maxDuration);
            looper->setFps(fps);
            looper->setPlaySpeed(speed);
        }
    }

}
