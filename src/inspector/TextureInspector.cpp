#include "TextureInspector.hpp"
#include "../tex/ShaderTex.h"

TextureInspector::TextureInspector() : backButton(20, 20, 100, 30),
                                       shaderButton(0, 0, 320, 30) {
    backButton.setFill(true);
    backButton.setColor(ofColor(100, 100, 100));
    shaderButton.setFill(false);
    shaderButton.setColor(ofColor(255, 255, 0));
}

void TextureInspector::setTexture(const ofTexture* texture, const string& name) {
    this->texture = texture;
    this->textureObj = nullptr;
    this->textureName = name;
    this->showLargeView = false;
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
}

void TextureInspector::clear() {
    texture = nullptr;
    textureObj = nullptr;
    textureName = "";
    showLargeView = false;
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

    ofTranslate(0, 230);
    drawTexturePreview();
    
    ofPopMatrix();
    
    if (showLargeView) {
        drawLargeTexture();
    }
}

void TextureInspector::drawTextureInfo() {
    ofSetColor(255);
    ofDrawBitmapString("Texture: " + textureName, 0, 0);
    
    // Check if texture is Shader-based
    if (textureObj != nullptr && textureObj->tex != nullptr) {
        shared_ptr<ShaderTex> shaderTex = std::dynamic_pointer_cast<ShaderTex>(textureObj->tex);
        if (shaderTex != nullptr) {
            // Draw clickable shader button
            ofPushMatrix();
            ofTranslate(0, 20);
            shaderButton.draw("");
            ofSetColor(255, 255, 0);
            string shaderPath = shaderTex->getShaderPath();
            if (shaderPath != "") {
                ofDrawBitmapString("Shader: " + shaderPath + " (click to inspect)", 5, 20);
            }
            else {
                ofDrawBitmapString("Shader: loaded (click to inspect)", 5, 20);
            }
            ofPopMatrix();
        }
    }
    
    if (texture != nullptr) {
        int width = texture->getWidth();
        int height = texture->getHeight();
        int depth = texture->getDepth();
        
        int yOffset = 0;
        if (textureObj != nullptr && textureObj->tex != nullptr) {
            shared_ptr<ShaderTex> shaderTex = std::dynamic_pointer_cast<ShaderTex>(textureObj->tex);
            if (shaderTex != nullptr) {
                yOffset = 50; // Make room for shader button
            }
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
    
    ofSetColor(255);
    texture->draw(0, 20, scaledWidth, scaledHeight);
    
    // Draw border
    ofNoFill();
    ofSetColor(255);
    ofDrawRectangle(0, 20, scaledWidth, scaledHeight);
    ofFill();
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

    // Draw texture name
    ofSetColor(255);
    ofDrawBitmapString(textureName, centerX, centerY - 20);

    // Draw texture
    ofSetColor(255);
    texture->draw(centerX, centerY, scaledWidth, scaledHeight);

    // Draw instructions
    ofDrawBitmapString("Left click to close", centerX, centerY + scaledHeight + 20);

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
        if (textureObj != nullptr && textureObj->tex != nullptr) {
            shared_ptr<ShaderTex> shaderTex = std::dynamic_pointer_cast<ShaderTex>(textureObj->tex);
            if (shaderTex != nullptr) {
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
        }
        
        if (showLargeView) {
            // Close large view
            showLargeView = false;
            return;
        }
        
        // Check if clicking on preview
        float baseOffsetX = 20;
        float baseOffsetY = 100 + 200; // Preview position
        float previewSize = 200;
        
        if (texture != nullptr) {
            float texWidth = texture->getWidth();
            float texHeight = texture->getHeight();
            float scale = std::min(previewSize / texWidth, previewSize / texHeight);
            float scaledWidth = texWidth * scale;
            float scaledHeight = texHeight * scale;
            
            float previewX = baseOffsetX;
            float previewY = baseOffsetY;
            
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

