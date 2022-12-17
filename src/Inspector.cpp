#include "Inspector.hpp"
#include "TexturePool.h"
#include "Buffer.hpp"

void Inspector::inspect(const vector<Layer*>& layers, const vector<OSCInput>& inputs) {
    this->layers = &layers;
    this->inputs = &inputs;
    
    ofPushMatrix();
    
    if (debugLayer < 0) {
        drawDebugGlobals();
    }
    else {
        drawDebugLayer(debugLayer);
    }
    
    ofPopMatrix();
}

void Inspector::drawDebugGlobals() {
    ofTranslate(20, 60);
    ofDrawBitmapString("Geometry", 0, -20);
    ofPushStyle();
    debugGeoms();
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Textures", 0, -20);
    ofPushStyle();
    debugGlobalTextures();
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Inputs", 0, -20);
    drawInputs();
}

void Inspector::drawDebugLayer(int i) {
    ofTranslate(20, 60);
    ofDrawBitmapString("Geometry", 0, -20);
    ofPushStyle();
    debugGeom(i);
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Default Texture", 0, -20);
    ofPushStyle();
    if (layers->at(i)->shader.hasDefaultTexture()) {
        debugTexture(layers->at(i)->shader.getDefaultTexture()->getTexture());
    }
    else {
        debugEmpty("not loaded");
    }
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Shader Textures", 0, -20);
    ofPushStyle();
    debugShaderTextures(i);
    ofPopStyle();
    
    ofTranslate(0, 140);
    ofDrawBitmapString("Shader Buffers", 0, -20);
    ofPushStyle();
    debugShaderBuffers(i);
    ofPopStyle();
}

void Inspector::debugGeoms() {
    for (int i=0; i<layers->size(); i++) {
        debugGeom(i);
    }
}

void Inspector::debugGeom(int i) {
    if (layers->at(i)->hasGeom()) {
        ofSetColor(255);
        ofPushMatrix();
        ofTranslate(i*120+60, 0);
        layers->at(i)->geom->getMesh().draw(OF_MESH_WIREFRAME);
        ofPopMatrix();
    }
}

void Inspector::debugGlobalTextures() {
    map<string, shared_ptr<Texture>>& pool = TexturePool::getPool(NULL);
    for (map<string, shared_ptr<Texture>>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        debugTexture(it->second->getTexture());
        ofTranslate(120, 0);
    }
}

void Inspector::debugShaderTextures(int i) {
    const map<string, shared_ptr<Texture>>& textures = layers->at(i)->shader.getTextures();
    for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
        debugTexture(it->second->getTexture());
        ofTranslate(120, 0);
    }
}

void Inspector::debugShaderBuffers(int i) {
    const map<string, shared_ptr<Buffer>>& buffers = layers->at(i)->shader.getBuffers();
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        debugTexture(it->second->getTexture());
        ofTranslate(120, 0);
    }
}

void Inspector::debugTexture(const ofTexture& tex) {
    ofSetColor(255);
    tex.draw(0, 0, 100, 100);
}

void Inspector::debugEmpty(string text) {
    ofFill();
    ofSetColor(0);
    ofDrawRectangle(0, 0, 100, 100);
    ofSetColor(255);
    ofDrawBitmapString(text, 8, 20);
}

void Inspector::drawInputs() {
    if (inputs->size() > 0) {
        for (int i=0; i<inputs->size(); i++) {
            ofPushMatrix();
            ofTranslate((i+1)*20 + i*200, 0);
            drawAmplitude(inputs->at(i));
            ofPopMatrix();
        }
    }
    // draw mfcc
}

void Inspector::drawAmplitude(const OSCInput& input) {
    ofPushStyle();
    ofNoFill();
    ofSetColor(225);
    ofDrawBitmapString(ofToString(input.ampScaled * 100.0, 0), 4, 18);
    ofDrawRectangle(0, 0, 100, 100);
    ofFill();
    ofDrawCircle(50, 50, input.ampScaled * 100.0f);
    ofBeginShape();
    ofSetColor(245, 58, 135);
    for (unsigned int i = 0; i < input.ampHist.size(); i++){
        float x = (float)i/2.f;
        if( i == 0 ) ofVertex(x, 100);
        ofVertex(x, 100 - input.ampHist[i] * 100.f);
        if( i == input.ampHist.size() -1 ) ofVertex(x, 100);
    }
    ofEndShape(false);
    ofPopStyle();
}
