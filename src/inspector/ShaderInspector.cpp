#include "ShaderInspector.hpp"
#include "../Texture.h"
#include "../Buffer.hpp"

ShaderInspector::ShaderInspector() : printUniformsButton(-10, -35, 190, 25),
                                       backButton(20, 20, 100, 30) {
    printUniformsButton.setFill(false);
    printUniformsButton.setColor(ofColor(255));
    backButton.setFill(true);
    backButton.setColor(ofColor(100, 100, 100));
}

void ShaderInspector::setShader(Shader* shader, const string& name) {
    this->shader = shader;
    this->shaderName = name;
}

void ShaderInspector::clear() {
    shader = nullptr;
    shaderName = "";
}

void ShaderInspector::draw() {
    // Draw back button
    ofPushStyle();
    ofSetColor(255);
    backButton.draw("Back");
    ofPopStyle();
    
    if (shader == nullptr) {
        ofDrawBitmapString("No shader selected", 20, 100);
        return;
    }
    
    ofPushMatrix();
    ofTranslate(20, 100);
    
    drawShaderInfo();
    ofTranslate(0, 80);
    
    printUniformsButton.draw("Print Shader Uniforms");
    ofTranslate(0, 30);
    
    // Draw uniforms in second column (starting from screen middle)
    ofPushMatrix();
    ofTranslate(ofGetWidth() / 2, 0);
    drawShaderUniforms();
    ofPopMatrix();
    
    // Keep textures and buffers in first column
    ofPushMatrix();
    drawShaderTextures();
    ofPopMatrix();

    ofPushMatrix();
    ofTranslate(0, 140);
    drawShaderBuffers();
    ofPopMatrix();
    
    ofPopMatrix();
}

void ShaderInspector::drawShaderInfo() {
    ofSetColor(255);
    if (!shaderName.empty()) {
        ofDrawBitmapString("Shader: " + shaderName, 0, -20);
    }
    
    if (shader->isLoaded()) {
        string shaderPath = shader->getShaderPath();
        if (shaderPath != "") {
            ofDrawBitmapString("Path: " + shaderPath, 0, 0);
        }
        else {
            ofDrawBitmapString("Status: loaded", 0, 0);
        }
        
        // Check if it's a shadertoy shader
        if (shader->isShaderLoaded()) {
            ofDrawBitmapString("Type: Standard Shader", 0, 20);
        }
        else {
            ofDrawBitmapString("Type: Shadertoy", 0, 20);
        }
    }
    else {
        ofDrawBitmapString("Status: not loaded", 0, 0);
    }
}

void ShaderInspector::drawShaderUniforms() {
    ofSetColor(255);
    ofDrawBitmapString("Shader Uniforms", 0, -20);
    
    if (shader->isShaderLoaded()) {
        const map<string, GLenum>& uniformTypes = shader->getUniformTypes();
        const map<string, string>& uniformValues = shader->getUniformValues();
        
        int y = 0;
        for (const auto& it : uniformTypes) {
            const string& name = it.first;
            GLenum type = it.second;
            string typeStr = "Unknown";
            
            // Convert GLenum to string
            if (type == GL_FLOAT) typeStr = "float";
            else if (type == GL_FLOAT_VEC2) typeStr = "vec2";
            else if (type == GL_FLOAT_VEC3) typeStr = "vec3";
            else if (type == GL_FLOAT_VEC4) typeStr = "vec4";
            else if (type == GL_INT) typeStr = "int";
            else if (type == GL_INT_VEC2) typeStr = "ivec2";
            else if (type == GL_INT_VEC3) typeStr = "ivec3";
            else if (type == GL_INT_VEC4) typeStr = "ivec4";
            else if (type == GL_FLOAT_MAT4) typeStr = "mat4";
            else if (type == GL_SAMPLER_2D) typeStr = "sampler2D";
            else if (type == GL_SAMPLER_3D) typeStr = "sampler3D";
            else if (type == GL_SAMPLER_CUBE) typeStr = "samplerCube";
            else if (type == GL_SAMPLER_BUFFER) typeStr = "samplerBuffer";
            else typeStr = "0x" + ofToString(type, 0, 16);
            
            string displayStr = name + ": " + typeStr;
            
            // Get value from the map
            auto valueIt = uniformValues.find(name);
            if (valueIt != uniformValues.end()) {
                displayStr += " = " + valueIt->second;
            }
            
            ofDrawBitmapString(displayStr, 0, y);
            y += 15;
            
            // Limit display to prevent overflow
            if (y > 300) {
                ofDrawBitmapString("... (more uniforms)", 0, y);
                break;
            }
        }
        
        if (uniformTypes.empty()) {
            ofDrawBitmapString("No uniforms found", 0, 0);
        }
    }
    else {
        ofDrawBitmapString("Shader not loaded", 0, 0);
    }
}

void ShaderInspector::drawShaderTextures() {
    ofSetColor(255);
    ofDrawBitmapString("Shader Textures", 0, -20);
    ofPushStyle();
    
    const map<string, shared_ptr<Texture>>& textures = shader->getTextures();
    float x = 0;
    for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
        if (!it->first.empty()) {
            ofSetColor(255);
            ofDrawBitmapString(it->first, x, -5);
        }
        ofSetColor(255);
        if (it->second->isLoaded()) {
            it->second->getTexture().draw(x, 0, 100, 100);
        }
        else {
            ofNoFill();
            ofSetColor(128);
            ofDrawRectangle(x, 0, 100, 100);
            ofFill();
            ofSetColor(255);
            ofDrawBitmapString("not loaded", x + 10, 50);
        }
        ofTranslate(120, 0);
        x += 120;
    }
    
    if (textures.empty()) {
        ofSetColor(128);
        ofDrawBitmapString("No textures", 0, 50);
    }
    
    ofPopStyle();
}

void ShaderInspector::drawShaderBuffers() {
    ofSetColor(255);
    ofDrawBitmapString("Shader Buffers", 0, -20);
    ofPushStyle();
    
    const map<string, shared_ptr<Buffer>>& buffers = shader->getBuffers();
    float x = 0;
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        if (!it->first.empty()) {
            ofSetColor(255);
            ofDrawBitmapString(it->first, x, -5);
        }
        ofSetColor(255);
        it->second->getTexture().draw(x, 0, 100, 100);
        ofTranslate(120, 0);
        x += 120;
    }
    
    if (buffers.empty()) {
        ofSetColor(128);
        ofDrawBitmapString("No buffers", 0, 50);
    }
    
    ofPopStyle();
}

void ShaderInspector::mousePressed(int x, int y, int button) {
    if (button == 0) { // Left mouse button
        // Check back button
        if (backButton.hitTest(x, y)) {
            clear();
            return;
        }
    }
}

void ShaderInspector::mouseReleased(int x, int y, int button) {
    if (shader == nullptr) {
        return;
    }
    
    float offsetX = 20;
    float offsetY = 60 + 80 + 30; // Position of print button
    float btnX = printUniformsButton.getX() + offsetX;
    float btnY = printUniformsButton.getY() + offsetY;
    
    if (printUniformsButton.hitTest(x - offsetX, y - offsetY)) {
        if (shader->isShaderLoaded()) {
            shader->getShader().printActiveUniforms();
        }
    }
}

void ShaderInspector::keyPressed(int key) {
    // Not used for now
}

