#include "LayerTex.h"
#include "ShaderTex.h"
#include "VideoTex.h"
#include "SketchTex.h"
#include "ImageTex.h"
#include "HPVideoTex.h"
#include "WebcamTex.h"
#include "ColorTex.h"

Tex* LayerTex::factory(string type, string path) {
    Tex *tex = NULL;
    auto it = SourceMap.find(type);
    if (it != SourceMap.end()) {
        switch (it->second) {
            case Source::VIDEO:
                tex = new VideoTex(path);
                break;
            case Source::HPV:
                tex = new HPVideoTex(path);
                break;
            case Source::SHADER:
                tex = new ShaderTex(path);
                break;
            case Source::SKETCH:
                tex = new SketchTex(path);
                break;
            case Source::IMAGE:
                tex = new ImageTex(path);
                break;
            case Source::WEBCAM:
                tex = new WebcamTex(path);
                break;
            case Source::COLOR:
                tex = new ColorTex(path);
                break;
        }
    }
    return tex;
}

Tex* LayerTex::factory(string source) {
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
                case Source::COLOR:
                    path = ColorTex::random();
                    break;
            }
        }
    }
    return factory(type, path);
}

void LayerTex::load(string source) {
    unload();
    bool explicitType = source.find(":") != string::npos;
    if (explicitType && source.substr(0, 4) != "http") {
        tex = factory(source);
    }
    else {
        string extension = ofFile(source).getExtension();
        for (int i=0; i<extension.size(); i++) {
            extension[i] = tolower(extension[i]);
        }
        if (extension == "frag") {
            tex = factory("shader", source);
        }
        else if (extension == "jpg" || extension == "jpeg" || extension == "png") {
            tex = factory("image", source);
        }
        else if (extension == "mov") {
            tex = factory("video", source);
        }
        else if (extension == "hpv") {
            tex = factory("hpv", source);
        }
        else {
            if (ColorTex::exists(source)) {
                tex = factory("color", source);
            }
            else if (SketchTex::exists(source)) {
                tex = factory("sketch", source);
            }
        }
    }
    if (tex == NULL) {
        ofLog() << "invalid source " << source;
    }
}

void LayerTex::choose(string type) {
    if (type == "") {
        auto it = SourceMap.begin();
        advance(it, int(ofRandom(SourceMap.size())));
        type = it->first;
    }
    tex = factory(type);
    if (tex == NULL) {
        ofLog() << "invalid source type " << type;
    }
}

void LayerTex::unload() {
    frames.clear();
    frames.resize(MAX_DELAY);
    if (tex != NULL) {
        delete tex;
        tex = NULL;
    }
}

void LayerTex::reload() {
    if (tex != NULL) {
        tex->reload();
    }
    else {
        ofLog() << "cannot reload layer " << index;
    }
}

void LayerTex::clear() {
    if (tex != NULL) {
        tex->clear();
    }
    else {
        ofLog() << "cannot clear layer " << index;
    }
}

void LayerTex::update() {
    tex->update(layer);
}

void LayerTex::drawToFbo() {
    curFbo = (curFbo + 1) % frames.size();
    ofFbo& fbo = frames[curFbo];
    if (!fbo.isAllocated()) {
        fbo.allocate(layer->size.x, layer->size.y);
    }
    fbo.begin();
    ofClear(0, 0, 0, 0);
    ofPushMatrix();
    layer->align();
    layer->rotate();
    if (layer->looper == NULL) {
        tex->draw(glm::vec3(0), layer->size);
    }
    else {
        ofSetColor(255);
        layer->looper->draw(0, 0, layer->size.x, layer->size.y);
    }
    fbo.end();
    ofPopMatrix();
}

void LayerTex::draw() {
    if (isLoaded()) {
        drawToFbo();
    }
}

void LayerTex::reset() {
    tex->reset();
}

const ofFbo& LayerTex::getFbo() const {
    int i = curFbo - layer->delay;
    while (i<0) i += frames.size();
    return frames[MIN(frames.size()-1, i)];
}

ofPixels& LayerTex::getPixels() const {
    return tex->getPixels();
}
