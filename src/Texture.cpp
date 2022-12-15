#include "Texture.h"
#include "SketchTex.h"
#include "Config.h"
#include "Layer.h"
#include "VariablePool.h"

void Texture::load(string source, const vector<float>& args) {
    _unload();
    bool explicitType = source.find(":") != string::npos;
    bool _isURL = Args::isURL(source);
    if (explicitType && !_isURL) {
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
        else if (extension == "mov" || extension == "mp4") {
            tex = Tex::factory("video", source, args);
        }
        else if (extension == "hpv") {
            tex = Tex::factory("hpv", source, args);
        }
        else if (extension == "html" || _isURL) {
            tex = Tex::factory("html", source, args);
        }
        else {
            if (Args::isHexColor(source)) {
                ofFloatColor color = Args::parseHexColor(source);
                vector<float> args1 = {color.r, color.g, color.b, color.a};
                tex = Tex::factory("color", "color", args1);
            }
            else if (SketchTex::exists(source)) {
                tex = Tex::factory("sketch", source, args);
            }
        }
    }
    if (tex == NULL) {
        ofLogError() << ("invalid source: " + source);
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

void Texture::choose(const ofxOscMessage& m) {
    _unload();
    string type = m.getNumArgs() > 1 ? m.getArgAsString(1) : "";
    vector<float> args;
    for (int i=2; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    tex = chooseTex(type, args);
    if (tex == NULL) {
        ofLogError() << ("chooseTex: invalid source type " + type);
    }
}

Tex* Texture::chooseTex(string type, const vector<float>& args) {
    if (type == "") {
        auto it = SourceMap.begin();
        advance(it, int(ofRandom(SourceMap.size())));
        type = it->first;
    }
    return Tex::factory(type, args);
}

void Texture::_unload() {
    frames.clear();
    frames.resize(numFrames);
    if (tex != NULL) {
        delete tex;
        tex = NULL;
    }
}

void Texture::unload() {
    _unload();
    vars.clear();
    VariablePool::cleanup(this);
}

void Texture::reload() {
    if (tex != NULL) {
        tex->reload();
    }
    else {
        ofLog() << ("cannot reload layer " + ofToString(index));
    }
}

void Texture::clear() {
    if (tex != NULL) {
        tex->clear();
    }
    else {
        ofLog() << ("cannot clear layer " + ofToString(index));
    }
}

void Texture::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    if (isLoaded()) {
        data.update(sounds, notes);
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
    texDraw(glm::vec2(0, 0), data.size);
    fbo.end();
}

void Texture::draw(Layer* layer) {
    if (isLoaded() && layer->delay == 0) {
        texDraw(layer->getVarVec3("pos"), layer->data.size);
        data.afterDraw(vars);
    }
    else if (hasTexture(layer->delay)) {
        // todo: does not work
        //ofPushStyle();
        //ofSetColor(getVarColor("tint"));
        const ofTexture& tex = getTexture(layer->delay);
        if (data.aspectRatio) {
            if (tex.getWidth() > tex.getHeight()) {
                tex.draw(layer->getVarVec3("pos"), layer->data.size.x, layer->data.size.x/tex.getWidth() * tex.getHeight());
            }
            else {
                tex.draw(layer->getVarVec3("pos"), layer->data.size.y/tex.getHeight() * tex.getWidth(), layer->data.size.y);
            }
        }
        else {
            tex.draw(layer->getVarVec3("pos"), layer->data.size.x, layer->data.size.y);
        }
        //ofPopStyle();
    }
}

void Texture::texDraw(const glm::vec2& pos, const glm::vec2 size) {
    // todo: does not work
    //ofPushStyle();
    //ofSetColor(getVarColor("tint"));
    if (looper == NULL) {
        tex->draw(pos, size);
    }
    else {
        looper->draw(pos.x, pos.y, size.x, size.y);
    }
    //ofPopStyle();
}

void Texture::setNumFrames(int value) {
    numFrames = value;
    frames.resize(numFrames);
}

void Texture::reset() {
    if (tex != NULL) {
        tex->reset();
    }
    vars.clear();
    VariablePool::cleanup(this);
    setVar("speed", 1.f);
    setVar("color", ofFloatColor(1.f, 1.f));
    setVar("tint", ofFloatColor(1.f, 1.f));
}

const ofFbo& Texture::getFrame(int delay) const {
    int i = curFbo - delay;
    while (i<0) i += frames.size();
    return frames[MIN(frames.size()-1, i)];
}

ofFbo& Texture::getFrame(int delay) {
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

ofTexture& Texture::getTexture(int delay) {
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
