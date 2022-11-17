#include "Layer.h"

void Layer::setup(int index)
{
    this->index = index;
    data.setup(this);
    varsConfig.clear();
}

void Layer::layout(Layout layout, int layoutIndex, int layoutTotal)
{
    switch (layout) {
        case Layout::COLUMN:
            pos = glm::vec3(0, ofGetHeight() / layoutTotal * layoutIndex, 0);
            data.size = glm::vec3(ofGetWidth(), ofGetHeight() / layoutTotal, 0);
            break;
        case Layout::ROW:
            pos = glm::vec3(ofGetWidth() / layoutTotal * layoutIndex, 0, 0);
            data.size = glm::vec3(ofGetWidth() / layoutTotal, ofGetHeight(), 0);
            break;
        case Layout::GRID: {
            int root = (int) sqrt(layoutTotal);
            pos = glm::vec3(ofGetWidth() / root * (layoutIndex % root), ofGetHeight() / root * floor(layoutIndex / root), 0);
            data.size = glm::vec3(ofGetWidth() / root, ofGetHeight() / root, 0);
            break;
        }
        case Layout::STACK:
            pos = glm::vec3(0, 0, 0);
            data.size = glm::vec3(ofGetWidth(), ofGetHeight(), 0);
            break;
    }
}

void Layer::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    if (shader.isLoaded() || shader.hasDefaultTexture() || hasGeom()) {
        data.update(sounds, notes, varsConfig);
        rotation += rotationSpeed;
        if (data.useRandomColor) {
            data.color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
        }
    }
    shader.update(sounds, notes);
    material.setup(matSettings);
}

void Layer::draw(const glm::vec3 &pos, const glm::vec2 &size) {
    //ofSetGlobalAmbientColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    
    ofPushMatrix();
    doAlign();
    doRotate();
    ofPushStyle();
    ofSetColor(data.getTint() * bri, alpha * 255);
    
    if (hasGeom() || shader.isLoaded()) {
        if (geom == NULL) {
            geom = GeomPool::getForLayer(getId());
        }
        if (shader.hasDefaultTexture()) {
            if (!geom->isLoaded()) {
                geom->load("plane");
            }
            if (!shader.isLoaded()) {
                shader.load("texture");
            }
        }
        
        ofTranslate(pos + data.size/2.f);
        doScale();
        ofScale(geom->getScale(data.size));
        
        if (shader.isLoaded()) {
            shader.begin(data, delay);
            shader.getShader().setUniform1i("index", index);
            shader.getShader().setUniform2f("offset", pos.x, pos.y);
            // todo: fix material
            //material.begin();
            geom->draw();
            //material.end();
            shader.end();
        }
        else {
            geom->draw();
        }
    }
    else if (shader.hasDefaultTexture()) {
        doScale();
        shader.getDefaultTexture()->draw(this);
    }
    
    ofPopStyle();
    ofPopMatrix();
}

void Layer::draw(int totalVisible) {
    if (shader.isLoaded() || shader.hasDefaultTexture() || hasGeom()) {
        if (data.visible) {
            switch (data.blendMode) {
                case OF_BLENDMODE_ALPHA:
                    ofSetColor(255, 255, 255, 255 / totalVisible);
                    break;
                default:
                    ofSetColor(255);
                    break;
            }
            ofEnableBlendMode(data.blendMode);
            draw(pos, data.size);
            ofDisableBlendMode();
        }
        data.afterDraw(varsConfig);
    }
}

void Layer::doScale() {
    if (scale.x < 0) {
        ofTranslate(-scale.x * data.size.x, 0);
    }
    if (scale.y < 0) {
        ofTranslate(0, -scale.y * data.size.y);
    }
    if (scale.z < 0) {
        ofTranslate(0, 0, -scale.z);
    }
    ofScale(scale);
}

void Layer::doAlign() {
    switch (alignH) {
        case OF_ALIGN_HORZ_CENTER:
            ofTranslate(ofGetWidth()/2.f - data.size.x * abs(scale.x) / 2.f, 0);
            break;
        case OF_ALIGN_HORZ_RIGHT:
            ofTranslate(ofGetWidth() - data.size.x * abs(scale.x), 0);
            break;
    }
    switch (alignV) {
        case OF_ALIGN_VERT_CENTER:
            ofTranslate(0, ofGetHeight()/2.f - data.size.y * abs(scale.y) / 2.f);
            break;
        case OF_ALIGN_VERT_BOTTOM:
            ofTranslate(0, ofGetHeight() - data.size.y * abs(scale.y));
            break;
    }
}

void Layer::doRotate() {
    float degrees = glm::length(rotation);
    if (degrees != 0) {
        glm::vec3 axis = glm::normalize(rotation);
        ofTranslate(rotationPoint.x * data.size.x * abs(scale.x), rotationPoint.y * data.size.y * abs(scale.y));
        ofRotateDeg(degrees, axis.x, axis.y, axis.z);
        ofTranslate(-rotationPoint.x * data.size.x * abs(scale.x), -rotationPoint.y * data.size.y * abs(scale.y));
    }
}

void Layer::reset() {
    resetTransform();
    geom = NULL;
    GeomPool::clean(_id);
    shader.reset();
    varsConfig.clear();
}

void Layer::resetTransform() {
    alpha = 1.f;
    bri = 1.f;
    rotation = glm::vec3(0, 0, 0);
    rotationPoint = glm::vec3(0, 0, 0);
    rotationSpeed = glm::vec3(0, 0, 0);
    scale = glm::vec3(1);
}
