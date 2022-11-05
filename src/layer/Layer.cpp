#include "Layer.h"

void Layer::setup(int index, string dataSource)
{
    this->index = index;
    tex.layer = this;
    data.layer = this;
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
    if (tex.isLoaded()) {
        data.update(sounds, notes);
        rotation += rotationSpeed;
        if (useRandomColor) {
            color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
        }
        tex.update();
    }
    if (looper != NULL) {
        looper->swapBuffers(/*forceSwap*/);
        if (tex.isFrameNew()){
            looper->addFrame(tex.getPixels());
        }
        looper->update();
    }
    geom.update();
}

void Layer::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    tex.draw();
    
    ofPushMatrix();
    transform();
    ofPushStyle();
    ofSetColor(tex.getTint() * bri, alpha * 255);
    geom.draw();
    ofPopStyle();
    ofPopMatrix();
}

void Layer::draw(int totalVisible) {
    if (tex.isLoaded()) {
        if (data.visible) {
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
        data.afterDraw();
    }
}

void Layer::transform() {
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
    ofTranslate(pos + size/2.f);
    ofScale(size / glm::vec3(100, -100, 100));
}

void Layer::align() {
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
}

void Layer::rotate() {
    float degrees = glm::length(rotation);
    if (degrees != 0) {
        glm::vec3 axis = glm::normalize(rotation);
        ofTranslate(rotationPoint.x * size.x * abs(scale.x), rotationPoint.y * size.y * abs(scale.y));
        ofRotateDeg(degrees, axis.x, axis.y, axis.z);
        ofTranslate(-rotationPoint.x * size.x * abs(scale.x), -rotationPoint.y * size.y * abs(scale.y));
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

void Layer::reset() {
    if (tex.isLoaded()) {
        resetTransform();
        tex.reset();
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
