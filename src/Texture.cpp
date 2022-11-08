#include "Texture.h"
#include "SketchTex.h"
#include "Config.h"
#include "Layer.h"

bool isColor(string path) {
    return (path.substr(0, 1) == "#" && path.size() == 7) || (path.substr(0, 2) == "0x" && path.size() == 8);
}

void Texture::load(string source, const vector<float>& args) {
    unload();
    bool explicitType = source.find(":") != string::npos;
    if (explicitType && source.substr(0, 4) != "http") {
        tex = Tex::factory(source, args);
    }
    else {
        string extension = ofFile(source).getExtension();
        for (int i=0; i<extension.size(); i++) {
            extension[i] = tolower(extension[i]);
        }
        if (extension == "frag") {
            tex = Tex::factory("shader", source, args);
        }
        else if (extension == "jpg" || extension == "jpeg" || extension == "png") {
            tex = Tex::factory("image", source, args);
        }
        else if (extension == "mov") {
            tex = Tex::factory("video", source, args);
        }
        else if (extension == "hpv") {
            tex = Tex::factory("hpv", source, args);
        }
        else {
            if (isColor(source)) {
                ofFloatColor color = ofFloatColor::fromHex(ofHexToInt(source));
                vector<float> args1 = {color.r, color.g, color.b, color.a};
                tex = Tex::factory("color", "color", args1);
            }
            else if (SketchTex::exists(source)) {
                tex = Tex::factory("sketch", source, args);
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
    tex = Tex::factory(type, args);
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

void Texture::update() {
    if (isLoaded()) {
        // todo: fix
//        if (fboSettings.width == 0) {
//            fboSettings.width = layer->size.x;
//        }
//        if (fboSettings.height == 0) {
//            fboSettings.height = layer->size.y;
//        }
        tex->update(data);
        
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
        data.allocate(fbo);
    }
    fbo.begin();
    ofClear(0, 0, 0, 0);
    if (looper == NULL) {
        tex->draw(data.size);
    }
    else {
        ofSetColor(255);
        looper->draw(0, 0, data.size.x, data.size.y);
    }
    fbo.end();
}

void Texture::draw(Layer* layer) {
    if (isLoaded() && layer->delay == 0) {
        tex->draw(layer->size);
    }
    else if (hasTexture(layer->delay)) {
        // todo: implement aspect ratio
        getTexture(layer->delay).draw(glm::vec2(0, 0), layer->size.x, layer->size.y);
    }
}

void Texture::setNumFrames(int value) {
    numFrames = value;
    frames.resize(numFrames);
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
    if (numFrames <= 1) {
        return isLoaded() && tex->getTexture().isAllocated();
    }
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
