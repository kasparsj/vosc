#include "Layer.h"
#include "Shader.h"
#include "Video.h"
#include "Sketch.h"
#include "Image.h"
#include "HPVideo.h"
#include "Webcam.h"
#include "Color.h"

Gen* Layer::factory(string type, string path) {
    Gen *gen = NULL;
    auto it = SourceMap.find(type);
    if (it != SourceMap.end()) {
        switch (it->second) {
            case Source::VIDEO:
                gen = new Video(path);
                break;
            case Source::HPV:
                gen = new HPVideo(path);
                break;
            case Source::SHADER:
                gen = new Shader(path);
                break;
            case Source::SKETCH:
                gen = new Sketch(path);
                break;
            case Source::IMAGE:
                gen = new Image(path);
                break;
            case Source::WEBCAM:
                gen = new Webcam(path);
                break;
            case Source::COLOR:
                gen = new Color(path);
                break;
        }
    }
    return gen;
}

Gen* Layer::factory(string source) {
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
                    path = Video::random();
                    break;
                case Source::HPV:
                    path = HPVideo::random();
                    break;
                case Source::SHADER:
                    path = Shader::random();
                    break;
                case Source::SKETCH:
                    path = Sketch::random();
                    break;
                case Source::IMAGE:
                    path = Image::random();
                    break;
                case Source::WEBCAM:
                    path = Webcam::random();
                    break;
                case Source::COLOR:
                    path = Color::random();
                    break;
            }
        }
    }
    return factory(type, path);
}

void Layer::setup(int index, string dataSource)
{
    this->index = index;
    dataSources.clear();
    if (dataSource != "") {
        dataSources.push_back(dataSource);
    }
}

void Layer::layout(Layout layout, int layoutIndex, int layoutTotal)
{
    switch (layout) {
        case Layout::COLUMN:
            pos = glm::vec3(0, ofGetHeight() / layoutTotal * layoutIndex, 0);
            size = glm::vec3(ofGetWidth(), ofGetHeight() / layoutTotal, 0);
            break;
        case Layout::ROW:
            pos = glm::vec3(ofGetWidth() / layoutTotal * layoutIndex, 0, 0);
            size = glm::vec3(ofGetWidth() / layoutTotal, ofGetHeight(), 0);
            break;
        case Layout::GRID: {
            int root = (int) sqrt(layoutTotal);
            pos = glm::vec3(ofGetWidth() / root * (layoutIndex % root), ofGetHeight() / root * floor(layoutIndex / root), 0);
            size = glm::vec3(ofGetWidth() / root, ofGetHeight() / root, 0);
            break;
        }
        case Layout::STACK:
            pos = glm::vec3(0, 0, 0);
            size = glm::vec3(ofGetWidth(), ofGetHeight(), 0);
            break;
    }
}

void Layer::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    if (data != NULL) {
        data->update(sounds, notes);
    }
    rotation += rotationSpeed;
    if (useRandomColor) {
        color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
    }
    if (gen != NULL) {
        gen->update(this);
    }
    if (looper != NULL) {
        looper->swapBuffers(/*forceSwap*/);
        if (gen->isFrameNew()){
            looper->addFrame(gen->getPixels());
        }
        looper->update();
    }
    geom.update();
    ofDisableArbTex();
}

void Layer::drawToFbo() {
    curFbo = (curFbo + 1) % frames.size();
    ofFbo& fbo = frames[curFbo];
    if (!fbo.isAllocated()) {
        fbo.allocate(size.x, size.y);
    }
    fbo.begin();
    ofClear(0, 0, 0, 0);
    ofPushMatrix();
    switch (alignH) {
        case OF_ALIGN_HORZ_CENTER:
            ofTranslate(ofGetWidth()/2.f - size.x * abs(scale.x) / 2.f, 0);
            break;
        case OF_ALIGN_HORZ_RIGHT:
            ofTranslate(ofGetWidth() - size.x * abs(scale.x), 0);
            break;
    }
    switch (alignV) {
        case OF_ALIGN_VERT_CENTER:
            ofTranslate(0, ofGetHeight()/2.f - size.y * abs(scale.y) / 2.f);
            break;
        case OF_ALIGN_VERT_BOTTOM:
            ofTranslate(0, ofGetHeight() - size.y * abs(scale.y));
            break;
    }
    float degrees = glm::length(rotation);
    if (degrees != 0) {
        glm::vec3 axis = glm::normalize(rotation);
        ofTranslate(rotationPoint.x * size.x * abs(scale.x), rotationPoint.y * size.y * abs(scale.y));
        ofRotateDeg(degrees, axis.x, axis.y, axis.z);
        ofTranslate(-rotationPoint.x * size.x * abs(scale.x), -rotationPoint.y * size.y * abs(scale.y));
    }
    if (looper == NULL) {
        gen->draw(glm::vec3(0), size);
    }
    else {
        ofSetColor(255);
        looper->draw(0, 0, size.x, size.y);
    }
    fbo.end();
    ofPopMatrix();
}

void Layer::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    if (gen != NULL) {
        drawToFbo();
    }
    ofPushMatrix();
    if (scale.x < 0) {
        ofTranslate(-scale.x * size.x, 0);
    }
    if (scale.y < 0) {
        ofTranslate(0, -scale.y * size.y);
    }
    if (scale.z < 0) {
        ofTranslate(0, 0, -scale.z * size.z);
    }
    ofScale(scale);
    int i = curFbo - delay;
    while (i<0) i += frames.size();
    ofPushStyle();
    ofSetColor(gen->getTint(this) * bri, alpha * 255);
    const ofFbo& fbo = frames[MIN(frames.size()-1, i)];
    const ofTexture& tex = fbo.getTexture();
    ofTranslate(pos + size/2.f);
    ofScale(size / glm::vec3(100, -100, 100));
    if (fbo.isAllocated() && tex.isAllocated()) {
        tex.bind();
        geom.getMesh().draw();
        tex.unbind();
    }
    else {
        geom.getMesh().draw();
    }
    if (drawWireframe) {
        geom.getMesh().drawWireframe();
    }
    ofPopStyle();
    ofPopMatrix();
}

void Layer::draw(int totalVisible) {
    if (data != NULL) {
        if (data->visible) {
            switch (blendMode) {
                case OF_BLENDMODE_ALPHA:
                    ofSetColor(255, 255, 255, 255 / totalVisible);
                    break;
                default:
                    ofSetColor(255);
                    break;
            }
            ofEnableBlendMode(blendMode);
            draw(pos, size);
            ofDisableBlendMode();
        }
        data->afterDraw();
    }
}

void Layer::setGeometry(string key) {
    if (Geom::exists(key)) {
        geom.set(key);
    }
}

void Layer::setDataSources(vector<string> ds) {
    dataSources.clear();
    addDataSources(ds);
}

void Layer::addDataSources(vector<string> ds) {
    for (int i=0; i<ds.size(); i++) {
        string dsName = ds[i];
        string dsMax = "";
        if (dsName.find(":") != string::npos) {
            dsMax = dsName.substr(dsName.find(":") + 1);
            dsName = dsName.substr(0, dsName.find(":"));
        }
        auto it = DataSourceMap.find(dsName);
        if (it != DataSourceMap.end()) {
            // todo: should hold struct instead of string
            dataSources.push_back(it->first + dsMax);
        }
        else {
            ofLog() << "invalid data source " << ds[i];
        }
    }
}

void Layer::load(string source) {
    unload();
    bool explicitType = source.find(":") != string::npos;
    if (explicitType && source.substr(0, 4) != "http") {
        gen = factory(source);
    }
    else {
        string extension = ofFile(source).getExtension();
        for (int i=0; i<extension.size(); i++) {
            extension[i] = tolower(extension[i]);
        }
        if (extension == "frag") {
            gen = factory("shader", source);
        }
        else if (extension == "jpg" || extension == "jpeg" || extension == "png") {
            gen = factory("image", source);
        }
        else if (extension == "mov") {
            gen = factory("video", source);
        }
        else if (extension == "hpv") {
            gen = factory("hpv", source);
        }
        else {
            if (Color::exists(source)) {
                gen = factory("color", source);
            }
            else if (Sketch::exists(source)) {
                gen = factory("sketch", source);
            }
        }
    }
    if (gen != NULL) {
        data = new LayerData(this);
    }
    else {
        ofLog() << "invalid source " << source;
    }
}

void Layer::choose(string type) {
    if (type == "") {
        auto it = SourceMap.begin();
        advance(it, int(ofRandom(SourceMap.size())));
        type = it->first;
    }
    gen = factory(type);
    if (gen != NULL) {
        data = new LayerData(this);
    }
    else {
        ofLog() << "invalid source type " << type;
    }
}

void Layer::unload() {
    frames.clear();
    frames.resize(MAX_DELAY);
    if (gen != NULL) {
        delete gen;
        gen = NULL;
    }
    if (data != NULL) {
        delete data;
        data = NULL;
    }
}

void Layer::reload() {
    if (gen != NULL) {
        gen->reload();
    }
    else {
        ofLog() << "cannot reload layer " << index;
    }
}

void Layer::clear() {
    if (gen != NULL) {
        gen->clear();
    }
    else {
        ofLog() << "cannot clear layer " << index;
    }
}

void Layer::reset() {
    if (gen != NULL) {
        resetTransform();
        gen->reset();
    }
    else {
        ofLog() << "cannot reset layer " << index;
    }
}

void Layer::resetTransform() {
    speed = 1.f;
    alpha = 1.f;
    bri = 1.f;
    color = ofFloatColor(0, 0);
    rotation = glm::vec3(0, 0, 0);
    rotationPoint = glm::vec3(0, 0, 0);
    rotationSpeed = glm::vec3(0, 0, 0);
    scale = glm::vec3(1);
}
