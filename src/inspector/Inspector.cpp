#include "Inspector.hpp"
#include "../TexturePool.h"
#include "../Buffer.hpp"
#include "../input/Inputs.hpp"

Inspector::Inspector() {
    // Set up callback for TextureInspector to open ShaderInspector
    textureInspector.onOpenShaderInspector = [this](Shader* shader, const string& name) {
        showingShaderInspector = true;
        showingTextureInspector = false;
        shaderInspector.setShader(shader, name);
    };
}

void Inspector::inspect(const vector<shared_ptr<Layer>>& layers) {
    this->layers = &layers;

    ofPushMatrix();

    if (showingTextureInspector) {
        textureInspector.draw();
    }
    else if (showingShaderInspector) {
        shaderInspector.draw();
    }
    else {
        // Main mode
        if (debugLayer < 0) {
            drawDebugGlobals();
        }
        else {
            drawDebugLayer(debugLayer);
        }
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

    // Draw clickable shader info area
    Button shaderInfoButton(0, -20, 300, 30);
    shaderInfoButton.setFill(false);
    shaderInfoButton.setColor(ofColor(255, 255, 0));
    shaderInfoButton.draw("");
    
    if (layers->at(i)->shader.isLoaded()) {
        string shaderPath = layers->at(i)->shader.getShaderPath();
        if (shaderPath != "") {
            ofDrawBitmapString("Shader: " + shaderPath + " (click to inspect)", 0, 0);
        }
        else {
            ofDrawBitmapString("Shader: loaded (click to inspect)", 0, 0);
        }
    }
    else {
        ofDrawBitmapString("Shader: not loaded (click to inspect)", 0, 0);
    }
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
    // Forward mouse events to active inspector
    if (showingTextureInspector) {
        textureInspector.mousePressed(x, y, button);
        // Check if back button was clicked (which clears the inspector)
        if (textureInspector.isEmpty()) {
            showingTextureInspector = false;
        }
        return;
    }
    else if (showingShaderInspector) {
        shaderInspector.mousePressed(x, y, button);
        // Check if back button was clicked (which clears the inspector)
        if (shaderInspector.isEmpty()) {
            showingShaderInspector = false;
        }
        return;
    }
    
    if (button == 0 || button == 2) { // Left or right mouse button
        // Close large texture view on left click
        if (largeTexture != nullptr) {
            largeTexture = nullptr;
            largeTextureName = "";
            return;
        }

        // Check texture buttons to open TextureInspector
        float baseOffsetX = 20; // Base offset from drawDebugGlobals/drawDebugLayer
        float baseOffsetY = 60; // Base offset
        if (debugLayer < 0) {
            // Global textures section
            float sectionOffsetY = 140; // Textures section offset
            float offsetY = baseOffsetY + sectionOffsetY;
            
            // Store texture objects for global textures
            map<string, shared_ptr<Texture>>& pool = TexturePool::getPool(NULL);
            int textureIndex = 0;
            for (map<string, shared_ptr<Texture>>::iterator it=pool.begin(); it!=pool.end(); ++it) {
                if (textureIndex < textureButtons.size() && 
                    textureButtons[textureIndex].hitTest(x - baseOffsetX, y - offsetY)) {
                    // Open TextureInspector with this texture
                    showingTextureInspector = true;
                    showingShaderInspector = false;
                    textureInspector.setTexture(it->second.get(), textureButtonNames[textureIndex]);
                    return;
                }
                textureIndex++;
            }
        }
        else {
            // Check shader info button
            float shaderInfoOffsetY = 140; // Shader info section offset (after Default Texture section)
            Button shaderInfoButton(0, -20, 300, 30);
            // Account for the -20 offset in the button position
            if (shaderInfoButton.hitTest(x - baseOffsetX, y - (baseOffsetY + shaderInfoOffsetY - 20))) {
                // Open ShaderInspector with this shader
                showingShaderInspector = true;
                showingTextureInspector = false;
                string shaderName = "Layer " + ofToString(debugLayer + 1) + " Shader";
                shaderInspector.setShader(&layers->at(debugLayer)->shader, shaderName);
                return;
            }
            
            // Check default texture first
            float defaultTexOffsetY = 140; // Default texture section
            Button defaultTexButton(0, 0, 100, 100);
            if (layers->at(debugLayer)->shader.hasDefaultTexture()) {
                if (defaultTexButton.hitTest(x - baseOffsetX, y - (baseOffsetY + defaultTexOffsetY))) {
                    // Open TextureInspector with default texture
                    showingTextureInspector = true;
                    showingShaderInspector = false;
                    shared_ptr<Texture> defaultTex = layers->at(debugLayer)->shader.getDefaultTexture();
                    textureInspector.setTexture(defaultTex.get(), "Default Texture");
                    return;
                }
            }
            
            // Check shader textures
            float shaderTexOffsetY = 140 + 140 + 30; // Shader textures section
            float offsetY = baseOffsetY + shaderTexOffsetY;
            
            const map<string, shared_ptr<Texture>>& textures = layers->at(debugLayer)->shader.getTextures();
            int textureIndex = 0;
            for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
                if (textureIndex < textureButtons.size() && 
                    textureButtons[textureIndex].hitTest(x - baseOffsetX, y - offsetY)) {
                    // Open TextureInspector with this texture
                    showingTextureInspector = true;
                    showingShaderInspector = false;
                    textureInspector.setTexture(it->second.get(), textureButtonNames[textureIndex]);
                    return;
                }
                textureIndex++;
            }
            
            // Check shader buffers (buffers don't have Texture objects, only ofTexture)
            float bufferOffsetY = 140 + 140 + 30 + 140; // Shader buffers section
            offsetY = baseOffsetY + bufferOffsetY;
            
            // For buffers, we can only pass the ofTexture since Buffer doesn't have a Texture object
            int bufferIndex = 0;
            const map<string, shared_ptr<Buffer>>& buffers = layers->at(debugLayer)->shader.getBuffers();
            for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
                int buttonIndex = textureIndex + bufferIndex;
                if (buttonIndex < textureButtons.size() && 
                    textureButtons[buttonIndex].hitTest(x - baseOffsetX, y - offsetY)) {
                    // Open TextureInspector with this buffer texture
                    showingTextureInspector = true;
                    showingShaderInspector = false;
                    textureInspector.setTexture(&it->second->getTexture(), textureButtonNames[buttonIndex]);
                    return;
                }
                bufferIndex++;
            }
        }
    }
}

void Inspector::mouseReleased(int x, int y, int button) {
    // Forward mouse events to active inspector
    if (showingTextureInspector) {
        textureInspector.mouseReleased(x, y, button);
        return;
    }
    else if (showingShaderInspector) {
        shaderInspector.mouseReleased(x, y, button);
        return;
    }
    
    // Main mode handling (print button moved to ShaderInspector)
}

void Inspector::keyPressed(int key) {
    // Forward key events to active inspector
    if (showingTextureInspector) {
        textureInspector.keyPressed(key);
        // ESC or 'q' to go back to main view
        if (key == OF_KEY_ESC || key == 'q') {
            showingTextureInspector = false;
            textureInspector.clear();
        }
        return;
    }
    else if (showingShaderInspector) {
        shaderInspector.keyPressed(key);
        // ESC or 'q' to go back to main view
        if (key == OF_KEY_ESC || key == 'q') {
            showingShaderInspector = false;
            shaderInspector.clear();
        }
        return;
    }
    
    // Main mode handling
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
