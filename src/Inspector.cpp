#include "Inspector.hpp"
#include "TexturePool.h"
#include "Buffer.hpp"
#include "Inputs.hpp"

Inspector::Inspector() : printUniformsButton(-10, -35, 190, 25) {
    printUniformsButton.setFill(false);
    printUniformsButton.setColor(ofColor(255));
}

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
    clearTextureButtons();
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
    clearTextureButtons();
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

    if (layers->at(i)->shader.isLoaded()) {
        string shaderPath = layers->at(i)->shader.getShaderPath();
        if (shaderPath != "") {
            ofDrawBitmapString("Shader: " + shaderPath, 0, -20);
        }
        else {
            ofDrawBitmapString("Shader: loaded", 0, -20);
        }
    }
    else {
        ofDrawBitmapString("Shader: not loaded", 0, -20);
    }
    ofTranslate(0, 30);

    printUniformsButton.draw("Print Shader Uniforms");
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
    // Create a button for this texture
    Button texButton(x, y, 100, 100);
    texButton.setFill(false);
    texButton.setColor(ofColor(255));
    
    // Store button info for hit testing
    textureButtons.push_back(texButton);
    textureButtonNames.push_back(name);
    textureButtonTextures.push_back(&tex);
    
    // Draw texture with button
    if (!name.empty()) {
        ofSetColor(255);
        ofDrawBitmapString(name, x, y - 5);
    }
    ofSetColor(255);
    tex.draw(x, y, 100, 100);
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
        // Check texture buttons
        float baseOffsetX = 20; // Base offset from drawDebugGlobals/drawDebugLayer
        float baseOffsetY = 60; // Base offset
        
        if (debugLayer < 0) {
            // Global textures section
            float sectionOffsetY = 140; // Textures section offset
            float offsetY = baseOffsetY + sectionOffsetY;
            
            for (size_t i = 0; i < textureButtons.size(); i++) {
                float btnX = textureButtons[i].getX() + baseOffsetX;
                float btnY = textureButtons[i].getY() + offsetY;
                
                if (textureButtons[i].hitTest(x - baseOffsetX, y - offsetY)) {
                    largeTexture = textureButtonTextures[i];
                    largeTextureName = textureButtonNames[i];
                    return;
                }
            }
        }
        else {
            // Check default texture first
            float defaultTexOffsetY = 140; // Default texture section
            Button defaultTexButton(0, 0, 100, 100);
            if (layers->at(debugLayer)->shader.hasDefaultTexture()) {
                if (defaultTexButton.hitTest(x - baseOffsetX, y - (baseOffsetY + defaultTexOffsetY))) {
                    largeTexture = &layers->at(debugLayer)->shader.getDefaultTexture()->getTexture();
                    largeTextureName = "Default Texture";
                    return;
                }
            }
            
            // Check shader textures
            float shaderTexOffsetY = 140 + 140 + 30; // Shader textures section
            float offsetY = baseOffsetY + shaderTexOffsetY;
            
            for (size_t i = 0; i < textureButtons.size(); i++) {
                if (textureButtons[i].hitTest(x - baseOffsetX, y - offsetY)) {
                    largeTexture = textureButtonTextures[i];
                    largeTextureName = textureButtonNames[i];
                    return;
                }
            }
            
            // Check shader buffers
            float bufferOffsetY = 140 + 140 + 30 + 140; // Shader buffers section
            offsetY = baseOffsetY + bufferOffsetY;
            
            // Texture buttons for buffers are added after shader textures
            // We need to check which section the button belongs to
            // For simplicity, check all buttons again with buffer offset
            for (size_t i = 0; i < textureButtons.size(); i++) {
                if (textureButtons[i].hitTest(x - baseOffsetX, y - offsetY)) {
                    largeTexture = textureButtonTextures[i];
                    largeTextureName = textureButtonNames[i];
                    return;
                }
            }
        }
    }
}

void Inspector::mouseReleased(int x, int y, int button) {
    if (debugLayer >= 0) {
        float offsetX = 20; // Base offset from drawDebugLayer
        float offsetY = 60 + 140 + 140 + 30; // Position of print button
        float btnX = printUniformsButton.getX() + offsetX;
        float btnY = printUniformsButton.getY() + offsetY;
        
        if (printUniformsButton.hitTest(x - offsetX, y - offsetY)) {
            if (layers->at(debugLayer)->shader.isLoaded()) {
                layers->at(debugLayer)->shader.getShader().printActiveUniforms();
            }
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

void Inspector::updateTextureButtons() {
    // This method can be used to update button positions if needed
    // Currently buttons are created in debugTexture
}

void Inspector::clearTextureButtons() {
    textureButtons.clear();
    textureButtonNames.clear();
    textureButtonTextures.clear();
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
