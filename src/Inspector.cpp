#include "Inspector.hpp"
#include "TexturePool.h"
#include "Buffer.hpp"
#include "Inputs.hpp"

void Inspector::inspect(const vector<shared_ptr<Layer>>& layers) {
    this->layers = &layers;

    ofPushMatrix();

    if (debugLayer < 0) {
        drawDebugGlobals();
    }
    else {
        drawDebugLayer(debugLayer);
    }

    ofPopMatrix();

    if (largeTexture != nullptr) {
        drawLargeTexture();
    }
}

void Inspector::drawDebugGlobals() {
    ofTranslate(20, 60);
    ofDrawBitmapString("Global Geometry", 0, -20);
    ofPushStyle();
    debugGeoms();
    ofPopStyle();

    ofTranslate(0, 140);
    ofDrawBitmapString("Global Textures", 0, -20);
    ofPushStyle();
    debugGlobalTextures();
    ofPopStyle();

    ofTranslate(0, 140);
    ofDrawBitmapString("Global Inputs", 0, -20);
    drawInputs();
}

void Inspector::drawDebugLayer(int i) {
    ofTranslate(20, 60);
    ofDrawBitmapString("Layer " + ofToString(debugLayer+1) + " Geometry", 0, -20);
    ofPushStyle();
    debugGeom(i);
    ofPopStyle();
    ofTranslate(0, 140);

    ofDrawBitmapString("Default Texture", 0, -20);
    ofPushStyle();
    if (layers->at(i)->shader.hasDefaultTexture()) {
        debugTexture(layers->at(i)->shader.getDefaultTexture()->getTexture(), "Default Texture", 0, 0);
    }
    else {
        debugEmpty("not loaded");
    }
    ofPopStyle();
    ofTranslate(0, 140);

    ofPushStyle();
    ofNoFill();
    ofDrawRectangle(-10, -35, 190, 25);
    ofDrawBitmapString("Print Shader Uniforms", 0, -20);
    ofPopStyle();
    ofTranslate(0, 30);

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
    if (layers->at(i)->hasGeom() && layers->at(i)->geom->hasMesh()) {
        ofSetColor(255);
        ofPushMatrix();
        ofTranslate(i*120+60, 0);
        // todo: scale geom
        layers->at(i)->geom->getMesh().draw(OF_MESH_WIREFRAME);
        ofPopMatrix();
    }
}

void Inspector::debugGlobalTextures() {
    ofPushMatrix();
    map<string, shared_ptr<Texture>>& pool = TexturePool::getPool(NULL);
    float x = 0;
    for (map<string, shared_ptr<Texture>>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        debugTexture(it->second->getTexture(), it->first, x, 0);
        ofTranslate(120, 0);
        x += 120;
    }
    ofPopMatrix();
}

void Inspector::debugShaderTextures(int i) {
    ofPushMatrix();
    const map<string, shared_ptr<Texture>>& textures = layers->at(i)->shader.getTextures();
    float x = 0;
    for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
        debugTexture(it->second->getTexture(), it->first, x, 0);
        ofTranslate(120, 0);
        x += 120;
    }
    ofPopMatrix();
}

void Inspector::debugShaderBuffers(int i) {
    ofPushMatrix();
    const map<string, shared_ptr<Buffer>>& buffers = layers->at(i)->shader.getBuffers();
    float x = 0;
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        debugTexture(it->second->getTexture(), it->first, x, 0);
        ofTranslate(120, 0);
        x += 120;
    }
    ofPopMatrix();
}

void Inspector::debugTexture(const ofTexture& tex, const string& name, float x, float y) {
    if (!name.empty()) {
        ofSetColor(255);
        ofDrawBitmapString(name, 0, -5);
    }
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
    const map<string, shared_ptr<OSCInput>>& inputs = Inputs::get().all();
    int i=0;
    for (const auto& kv : inputs) {
        ofPushMatrix();
        ofTranslate((i+1)*20 + i*200, 0);
        drawAmplitude(kv.second);
        ofPopMatrix();
        i++;
    }
    // draw mfcc
}

void Inspector::drawAmplitude(const shared_ptr<OSCInput> input) {
    ofPushStyle();
    ofNoFill();
    ofSetColor(225);
    ofDrawBitmapString(ofToString(input->ampScaled * 100.0, 0), 4, 18);
    ofDrawRectangle(0, 0, 100, 100);
    ofFill();
    ofDrawCircle(50, 50, input->ampScaled * 100.0f);
    ofBeginShape();
    ofSetColor(245, 58, 135);
    for (unsigned int i = 0; i < input->ampHist.size(); i++){
        float x = (float)i/2.f;
        if( i == 0 ) ofVertex(x, 100);
        ofVertex(x, 100 - input->ampHist[i] * 100.f);
        if( i == input->ampHist.size() -1 ) ofVertex(x, 100);
    }
    ofEndShape(false);
    ofPopStyle();
}

void Inspector::mousePressed(int x, int y, int button) {
    if (button == 0) { // Left mouse button
        // Close large texture view on left click
        if (largeTexture != nullptr) {
            largeTexture = nullptr;
            largeTextureName = "";
            return;
        }
    }
    else if (button == 2) { // Right mouse button
        // Check if clicking on a texture in global view
        if (debugLayer < 0) {
            float baseX = 20;
            float baseY = 60 + 140; // Textures section
            map<string, shared_ptr<Texture>>& pool = TexturePool::getPool(NULL);
            float texX = baseX;
            for (map<string, shared_ptr<Texture>>::iterator it=pool.begin(); it!=pool.end(); ++it) {
                if (checkTextureClick(x, y, button, texX, baseY)) {
                    largeTexture = &it->second->getTexture();
                    largeTextureName = it->first;
                    return;
                }
                texX += 120;
            }
        }
        else {
            // Check default texture
            float baseX = 20;
            float baseY = 60 + 140; // Default texture section
            if (layers->at(debugLayer)->shader.hasDefaultTexture()) {
                if (checkTextureClick(x, y, button, baseX, baseY)) {
                    largeTexture = &layers->at(debugLayer)->shader.getDefaultTexture()->getTexture();
                    largeTextureName = "Default Texture";
                    return;
                }
            }

            // Check shader textures
            baseY = 60 + 140 + 140 + 30; // Shader textures section
            const map<string, shared_ptr<Texture>>& textures = layers->at(debugLayer)->shader.getTextures();
            float texX = baseX;
            for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
                if (checkTextureClick(x, y, button, texX, baseY)) {
                    largeTexture = &it->second->getTexture();
                    largeTextureName = it->first;
                    return;
                }
                texX += 120;
            }

            // Check shader buffers
            baseY = 60 + 140 + 140 + 30 + 140; // Shader buffers section
            const map<string, shared_ptr<Buffer>>& buffers = layers->at(debugLayer)->shader.getBuffers();
            texX = baseX;
            for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
                if (checkTextureClick(x, y, button, texX, baseY)) {
                    largeTexture = &it->second->getTexture();
                    largeTextureName = it->first;
                    return;
                }
                texX += 120;
            }
        }
    }
}

void Inspector::mouseReleased(int x, int y, int button) {
    if (x > 10 && x < 200 && y > 305 && y < 330) {
        if (layers->at(debugLayer)->shader.isLoaded()) {
            layers->at(debugLayer)->shader.getShader().printActiveUniforms();
        }
    }
}

void Inspector::keyPressed(int key) {
    if (key == '0') {
        debugLayer = -1;
    }
    else {
        debugLayer = key - '1';
    }
}

bool Inspector::checkTextureClick(int x, int y, int button, float texX, float texY) {
    // Account for the translation offsets in the drawing functions
    float offsetX = 20; // Base offset from drawDebugGlobals/drawDebugLayer
    float offsetY = 60; // Base offset
    float actualX = offsetX + texX;
    float actualY = offsetY + texY;

    return (x >= actualX && x <= actualX + 100 && y >= actualY && y <= actualY + 100);
}

void Inspector::drawLargeTexture() {
    if (largeTexture == nullptr) return;

    ofPushStyle();
    ofPushMatrix();

    // Draw semi-transparent background
    ofSetColor(0, 0, 0, 200);
    ofFill();
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    // Center the large texture
    float texWidth = largeTexture->getWidth();
    float texHeight = largeTexture->getHeight();
    float maxWidth = ofGetWidth() * 0.8f;
    float maxHeight = ofGetHeight() * 0.8f;

    float scale = std::min(maxWidth / texWidth, maxHeight / texHeight);
    float scaledWidth = texWidth * scale;
    float scaledHeight = texHeight * scale;

    float centerX = (ofGetWidth() - scaledWidth) * 0.5f;
    float centerY = (ofGetHeight() - scaledHeight) * 0.5f;

    // Draw texture name
    ofSetColor(255);
    ofDrawBitmapString(largeTextureName, centerX, centerY - 20);

    // Draw texture
    ofSetColor(255);
    largeTexture->draw(centerX, centerY, scaledWidth, scaledHeight);

    // Draw instructions
    ofDrawBitmapString("Left click to close", centerX, centerY + scaledHeight + 20);

    ofPopMatrix();
    ofPopStyle();
}
