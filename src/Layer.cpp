#include "Layer.h"
#include "Shader.h"
#include "Video.h"
#include "Sketch.h"
#include "Image.h"
#include "Three.h"
#include "HPVideo.h"
#include "Webcam.h"

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
            case Source::THREE:
                gen = new Three(path);
                break;
            case Source::WEBCAM:
                gen = new Webcam(path);
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
                case Source::THREE:
                    path = Three::random();
                    break;
                case Source::WEBCAM:
                    path = Webcam::random();
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
    rotAngle += rotSpeed;
    if (useRandomColor) {
        color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
    }
    if (gen != NULL) {
        gen->update(this);
    }
}

void Layer::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    if (gen != NULL) {
        ofPushStyle();
        ofSetColor(gen->getTint(this) * bri, alpha * 255);
        ofPushMatrix();
        // todo: centering should be optional, also fix with scale
        ofTranslate(size.x/2.f, size.y/2.f);
        ofRotateDeg(rotAngle, rotAxis.x, rotAxis.y, rotAxis.z);
        ofScale(scale);
        gen->draw(pos, size);
        ofPopMatrix();
        ofPopStyle();
    }
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
            if (Three::exists(source)) {
                gen = factory("3d", source);
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
    rotAngle = 0;
    rotAxis = glm::vec3(0, 1, 0);
    rotSpeed = 0;
    scale = glm::vec3(1);
}
