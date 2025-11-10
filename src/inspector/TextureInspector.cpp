#include "TextureInspector.hpp"
#include "../tex/ShaderTex.h"

TextureInspector::TextureInspector() : backButton(20, 20, 100, 30),
                                       shaderButton(0, 0, 320, 30),
                                       isStaticButton(0, 0, 160, 25),
                                       needsUpdateButton(0, 0, 160, 25),
                                       currentLayerIndex(0) {
    backButton.setFill(true);
    backButton.setColor(ofColor(100, 100, 100));
    shaderButton.setFill(false);
    shaderButton.setColor(ofColor(255, 255, 0));
    isStaticButton.setFill(true);
    isStaticButton.setColor(ofColor(100, 150, 100));
    needsUpdateButton.setFill(true);
    needsUpdateButton.setColor(ofColor(100, 150, 100));
}

TextureInspector::~TextureInspector() {
    // TextureArrayDraw will be cleaned up automatically
}

void TextureInspector::setTexture(const ofTexture* texture, const string& name) {
    this->texture = texture;
    this->textureObj = nullptr;
    this->textureName = name;
    this->showLargeView = false;
    this->currentLayerIndex = 0;
}

void TextureInspector::setTexture(Texture* texture, const string& name) {
    this->textureObj = texture;
    if (texture != nullptr && texture->isLoaded()) {
        this->texture = &texture->getTexture();
    }
    else {
        this->texture = nullptr;
    }
    this->textureName = name;
    this->showLargeView = false;
    this->currentLayerIndex = 0;
}

void TextureInspector::clear() {
    texture = nullptr;
    textureObj = nullptr;
    textureName = "";
    showLargeView = false;
    currentLayerIndex = 0;
}


void TextureInspector::draw() {
    // Draw back button
    ofPushStyle();
    ofSetColor(255);
    backButton.draw("Back");
    ofPopStyle();
    
    if (texture == nullptr) {
        ofDrawBitmapString("No texture selected", 20, 100);
        return;
    }
    
    ofPushMatrix();

    ofTranslate(20, 100);
    drawTextureInfo();
    
    ofTranslate(0, getPreviewOffsetY());
    drawTexturePreview();
    
    ofPopMatrix();
    
    // Draw shader passes in second column
    drawShaderPasses();
    
    // Draw vars in second column after shader passes
    if (textureObj != nullptr) {
        int columnX = ofGetWidth() / 2;
        int startY = 100; // Base start position
        if (!textureObj->passes.empty()) {
            // Add space for shader passes section
            startY += 20 + (textureObj->passes.size() * 20) + 20; // Title + items + spacing
        }
        varsInspector.draw(textureObj, columnX, startY);
    }
    
    if (showLargeView) {
        drawLargeTexture();
    }
}

void TextureInspector::drawTextureInfo() {
    ofSetColor(255);
    ofDrawBitmapString("Texture: " + textureName, 0, 0);
    
    // Check if texture is Shader-based
    if (textureObj != nullptr && textureObj->hasShader()) {
        shared_ptr<ShaderTex> shaderTex = std::dynamic_pointer_cast<ShaderTex>(textureObj->tex);
        // Draw clickable shader button
        ofPushMatrix();
        ofTranslate(0, 20);
        shaderButton.draw("");
        ofSetColor(255, 255, 0);
        string shaderPath = shaderTex->getShaderPath();
        if (shaderPath != "") {
            ofDrawBitmapString("Shader: " + shaderPath, 5, 20);
        }
        else {
            ofDrawBitmapString("Shader: loaded (click to inspect)", 5, 20);
        }
        ofPopMatrix();
    }
    
    if (texture != nullptr) {
        int width = texture->getWidth();
        int height = texture->getHeight();
        int depth = texture->getDepth();
        
        int yOffset = 0;
        if (textureObj != nullptr && textureObj->hasShader()) {
            yOffset = 50; // Make room for shader button
        }
        
        ofDrawBitmapString("Size: " + ofToString(width) + " x " + ofToString(height), 0, 20 + yOffset);
        if (depth > 0) {
            ofDrawBitmapString("Depth: " + ofToString(depth), 0, 40 + yOffset);
        }
        
        // Get texture format info
        GLint internalFormat = texture->getTextureData().glInternalFormat;
        ofDrawBitmapString("Internal Format: 0x" + ofToString(internalFormat, 0, 16), 0, 60 + yOffset);
        
        // Texture target
        GLenum target = texture->getTextureData().textureTarget;
        string targetStr = "Unknown";
        if (target == GL_TEXTURE_2D) targetStr = "GL_TEXTURE_2D";
        else if (target == GL_TEXTURE_RECTANGLE) targetStr = "GL_TEXTURE_RECTANGLE";
        else if (target == GL_TEXTURE_3D) targetStr = "GL_TEXTURE_3D";
        else if (target == GL_TEXTURE_CUBE_MAP) targetStr = "GL_TEXTURE_CUBE_MAP";
        else if (target == GL_TEXTURE_2D_ARRAY) targetStr = "GL_TEXTURE_2D_ARRAY";
        ofDrawBitmapString("Target: " + targetStr, 0, 80 + yOffset);
        
        // Wrap modes
        GLint wrapH = texture->getTextureData().wrapModeHorizontal;
        GLint wrapV = texture->getTextureData().wrapModeVertical;
        ofDrawBitmapString("Wrap H: " + ofToString(wrapH), 0, 100 + yOffset);
        ofDrawBitmapString("Wrap V: " + ofToString(wrapV), 0, 120 + yOffset);
        
        // Filters
        GLint minFilter = texture->getTextureData().minFilter;
        GLint magFilter = texture->getTextureData().magFilter;
        ofDrawBitmapString("Min Filter: " + ofToString(minFilter), 0, 140 + yOffset);
        ofDrawBitmapString("Mag Filter: " + ofToString(magFilter), 0, 160 + yOffset);
        
        // Static and needsUpdate toggle buttons
        if (textureObj != nullptr) {
            // Draw isStatic toggle button
            ofPushMatrix();
            ofTranslate(0, 180 + yOffset);
            bool isStatic = textureObj->isStatic;
            isStaticButton.setColor(isStatic ? ofColor(100, 200, 100) : ofColor(200, 100, 100));
            isStaticButton.draw(isStatic ? "isStatic: true" : "isStatic: false");
            ofPopMatrix();
            
            // Draw needsUpdate toggle button
            ofPushMatrix();
            ofTranslate(0, 210 + yOffset);
            bool needsUpdate = textureObj->needsUpdate;
            needsUpdateButton.setColor(needsUpdate ? ofColor(100, 200, 100) : ofColor(200, 100, 100));
            needsUpdateButton.draw(needsUpdate ? "needsUpdate: true" : "needsUpdate: false");
            ofPopMatrix();
        }
    }
}

void TextureInspector::drawShaderPasses() {
    if (textureObj == nullptr || textureObj->passes.empty()) {
        return;
    }
    
    // Draw in second column starting from screen middle
    int columnX = ofGetWidth() / 2;
    int startY = 100; // Same vertical start as first column
    
    ofSetColor(255);
    ofDrawBitmapString("Shader Passes:", columnX, startY);
    
    for (size_t i = 0; i < textureObj->passes.size(); i++) {
        int yPos = startY + 20 + (i * 20);
        string passPath = textureObj->passes[i]->getShaderPath();
        // Extract just the filename from the path
        string filename = ofFilePath::getFileName(passPath);
        if (filename == "") {
            filename = passPath; // Fallback to full path if no filename
        }
        ofDrawBitmapString("  " + ofToString(i + 1) + ". " + filename, columnX, yPos);
    }
}


void TextureInspector::drawTexturePreview() {
    if (texture == nullptr) return;
    
    ofSetColor(255);
    ofDrawBitmapString("Preview (click to enlarge)", 0, 0);
    
    // Draw preview texture
    float previewSize = 200;
    float texWidth = texture->getWidth();
    float texHeight = texture->getHeight();
    float scale = std::min(previewSize / texWidth, previewSize / texHeight);
    float scaledWidth = texWidth * scale;
    float scaledHeight = texHeight * scale;
    
    float previewX = 0;
    float previewY = 20;
    
    ofSetColor(255);
    
    // If texture has depth > 1, use tex_array shader to display specific layer
    int depth = texture->getDepth();
    if (depth > 1) {
        drawArrayTexture(previewX, previewY, scaledWidth, scaledHeight);
    } else {
        // Normal 2D texture
        texture->draw(previewX, previewY, scaledWidth, scaledHeight);
    }
    
    // Draw border
    ofNoFill();
    ofSetColor(255);
    ofDrawRectangle(previewX, previewY, scaledWidth, scaledHeight);
    ofFill();
}

void TextureInspector::drawArrayTexture(float x, float y, float width, float height) {
    if (texture == nullptr) return;
    
    textureArrayDraw.draw(const_cast<ofTexture&>(*texture), (int)x, (int)y, (int)width, (int)height, currentLayerIndex);
}

void TextureInspector::drawLargeTexture() {
    if (texture == nullptr) return;

    ofPushStyle();
    ofPushMatrix();

    // Draw semi-transparent background
    ofSetColor(0, 0, 0, 200);
    ofFill();
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    // Center the large texture
    float texWidth = texture->getWidth();
    float texHeight = texture->getHeight();
    float maxWidth = ofGetWidth() * 0.8f;
    float maxHeight = ofGetHeight() * 0.8f;

    float scale = std::min(maxWidth / texWidth, maxHeight / texHeight);
    float scaledWidth = texWidth * scale;
    float scaledHeight = texHeight * scale;

    float centerX = (ofGetWidth() - scaledWidth) * 0.5f;
    float centerY = (ofGetHeight() - scaledHeight) * 0.5f;

    // Draw texture name and layer info
    ofSetColor(255);
    string displayName = textureName;
    int depth = texture->getDepth();
    if (depth > 1) {
        displayName += " (Layer " + ofToString(currentLayerIndex) + " / " + ofToString(depth - 1) + ")";
        displayName += " - Click to cycle layers";
    }
    ofDrawBitmapString(displayName, centerX, centerY - 20);

    // Draw texture
    ofSetColor(255);
    
    // If texture has depth > 1, use tex_array shader to display specific layer
    if (depth > 1) {
        drawArrayTexture(centerX, centerY, scaledWidth, scaledHeight);
    } else {
        // Normal 2D texture
        texture->draw(centerX, centerY, scaledWidth, scaledHeight);
    }

    // Draw instructions
    string instructions = "Left click to close";
    if (depth > 1) {
        instructions += " | Click texture to cycle layers";
    }
    ofDrawBitmapString(instructions, centerX, centerY + scaledHeight + 20);

    ofPopMatrix();
    ofPopStyle();
}

void TextureInspector::mousePressed(int x, int y, int button) {
    if (button == 0) { // Left mouse button
        // Check back button
        if (backButton.hitTest(x, y)) {
            clear();
            return;
        }
        
        // Check shader button
        if (textureObj != nullptr && textureObj->hasShader()) {
            shared_ptr<ShaderTex> shaderTex = std::dynamic_pointer_cast<ShaderTex>(textureObj->tex);
            float shaderButtonX = 20;
            float shaderButtonY = 100 + 20; // Position of shader button
            if (shaderButton.hitTest(x - shaderButtonX, y - shaderButtonY)) {
                // Open ShaderInspector
                if (onOpenShaderInspector) {
                    onOpenShaderInspector(shaderTex.get(), textureName + " Shader");
                }
                return;
            }
        }
        
        // Check isStatic button
        if (textureObj != nullptr) {
            // Calculate yOffset for button position (same as in drawTextureInfo)
            int yOffset = 0;
            if (textureObj->hasShader()) {
                yOffset = 50; // Make room for shader button
            }
            // Button position: base translation (20, 100) + drawTextureInfo offset (0, 180 + yOffset)
            float isStaticButtonX = 20;
            float isStaticButtonY = 100 + 180 + yOffset;
            if (isStaticButton.hitTest(x - isStaticButtonX, y - isStaticButtonY)) {
                // Toggle isStatic
                textureObj->isStatic = !textureObj->isStatic;
                return;
            }
        }
        
        // Check needsUpdate button
        if (textureObj != nullptr) {
            // Calculate yOffset for needsUpdate button position (same as in drawTextureInfo)
            int yOffset = 0;
            if (textureObj->hasShader()) {
                yOffset = 50; // Make room for shader button
            }
            // Button position: base translation (20, 100) + drawTextureInfo offset (0, 210 + yOffset)
            float needsUpdateButtonX = 20;
            float needsUpdateButtonY = 100 + 210 + yOffset;
            if (needsUpdateButton.hitTest(x - needsUpdateButtonX, y - needsUpdateButtonY)) {
                // Toggle needsUpdate
                textureObj->needsUpdate = !textureObj->needsUpdate;
                return;
            }
        }
        
        if (showLargeView) {
            // Check if clicking on the texture (for layer cycling)
            if (texture != nullptr) {
                float texWidth = texture->getWidth();
                float texHeight = texture->getHeight();
                float maxWidth = ofGetWidth() * 0.8f;
                float maxHeight = ofGetHeight() * 0.8f;
                float scale = std::min(maxWidth / texWidth, maxHeight / texHeight);
                float scaledWidth = texWidth * scale;
                float scaledHeight = texHeight * scale;
                float centerX = (ofGetWidth() - scaledWidth) * 0.5f;
                float centerY = (ofGetHeight() - scaledHeight) * 0.5f;
                
                // Check if click is on the texture
                if (x >= centerX && x <= centerX + scaledWidth &&
                    y >= centerY && y <= centerY + scaledHeight) {
                    // Cycle through layers if texture has depth > 1
                    int depth = texture->getDepth();
                    if (depth > 1) {
                        currentLayerIndex = (currentLayerIndex + 1) % depth;
                    }
                    return;
                }
            }
            
            // Close large view if clicking outside texture
            showLargeView = false;
            return;
        }
        
        // Check if clicking on preview
        float baseOffsetX = 20;
        float previewSize = 200;
        
        if (texture != nullptr) {
            float texWidth = texture->getWidth();
            float texHeight = texture->getHeight();
            float scale = std::min(previewSize / texWidth, previewSize / texHeight);
            float scaledWidth = texWidth * scale;
            float scaledHeight = texHeight * scale;
            
            float previewX = baseOffsetX;
            float previewY = getPreviewOffsetY() + 110;
            
            if (x >= previewX && x <= previewX + scaledWidth &&
                y >= previewY && y <= previewY + scaledHeight) {
                showLargeView = true;
            }
        }
    }
}

void TextureInspector::mouseReleased(int x, int y, int button) {
    // Not used for now
}

void TextureInspector::keyPressed(int key) {
    if (key == OF_KEY_ESC || key == 'q') {
        showLargeView = false;
    }
}

