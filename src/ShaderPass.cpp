#include "ShaderPass.h"

ShaderPass::ShaderPass() : allocated(false), outputSize(0, 0, 0), widthSet(false), heightSet(false), depthSet(false) {
}

ShaderPass::~ShaderPass() {
    clear();
}

void ShaderPass::setup(const string& shaderPath, float width, float height, float depth) {
    this->shaderPath = shaderPath;
    this->widthSet = (width >= 0);
    this->heightSet = (height >= 0);
    this->depthSet = (depth >= 0);
    
    // Store the specified values (or 0 if not set, will be inherited in update)
    this->outputSize = glm::vec3(
        widthSet ? width : 0,
        heightSet ? height : 0,
        depthSet ? depth : 0
    );
    
    // Load shader
    // Try loading as vertex + fragment shader first (if shaderPath is a fragment shader)
    // Otherwise, ofShader will try to load it as a single shader file
    if (!shader.load(shaderPath)) {
        ofLogError("ShaderPass") << "Could not load shader: " << shaderPath;
        return;
    }
    
    // FBO will be allocated in update() when we know the actual size
    allocated = false;
}

void ShaderPass::update(ofTexture& inputTexture, TexData& texData) {
    if (!shader.isLoaded()) {
        return;
    }
    
    // Get current size from data
#if ALLOW_TEX_2D_ARRAY
    glm::vec3 currentSize = texData.getSize();
#else
    glm::vec2 currentSize2D = texData.getSize();
    glm::vec3 currentSize = glm::vec3(currentSize2D.x, currentSize2D.y, 0);
#endif
    
    // Inherit dimensions from data if not set
    float finalWidth = widthSet ? outputSize.x : currentSize.x;
    float finalHeight = heightSet ? outputSize.y : currentSize.y;
    float finalDepth = depthSet ? outputSize.z : currentSize.z;
    
    // Update outputSize with final values
    outputSize = glm::vec3(finalWidth, finalHeight, finalDepth);
    
    // Check if dimensions have changed and update TexData
    if (widthSet || heightSet || depthSet) {
        // Only update TexData if we're setting explicit dimensions
#if ALLOW_TEX_2D_ARRAY
        if (currentSize.x != outputSize.x || currentSize.y != outputSize.y || currentSize.z != outputSize.z) {
            texData.setSize(outputSize.x, outputSize.y, outputSize.z);
        }
#else
        if (currentSize.x != outputSize.x || currentSize.y != outputSize.y) {
            texData.setSize(outputSize.x, outputSize.y);
        }
#endif
    }
    
    // Check if FBO needs to be allocated or reallocated
    if (!allocated || fbo.getWidth() != outputSize.x || fbo.getHeight() != outputSize.y) {
        if (allocated) {
            fbo.clear();
        }
        
        // Allocate FBO
        ofFbo::Settings settings;
        settings.width = outputSize.x;
        settings.height = outputSize.y;
        if (outputSize.z > 0) {
            settings.depthStencilAsTexture = true;
            // For texture arrays, we'd need GL_TEXTURE_2D_ARRAY
            // For now, just allocate as 2D
        }
        settings.internalformat = GL_RGBA;
        settings.numColorbuffers = 1;
        settings.useDepth = false;
        settings.useStencil = false;
        
        fbo.allocate(settings);
        allocated = true;
    }
    
    // Render input texture through shader to FBO
    fbo.begin();
    ofClear(0, 0, 0, 0);
    
    shader.begin();
    
    // Set up standard uniforms (similar to Shader::begin)
    glm::vec2 size = glm::vec2(outputSize.x, outputSize.y);
    shader.setUniform1f("time", texData.time);
    shader.setUniform2f("resolution", size.x, size.y);
    shader.setUniform1i("random", texData.randomSeed);
    
    // Set input texture as uniform
    shader.setUniformTexture("srctex", inputTexture, 0);
    
    // Draw fullscreen quad
    ofDrawRectangle(0, 0, outputSize.x, outputSize.y);
    
    shader.end();
    fbo.end();
}

ofTexture& ShaderPass::getTexture() {
    return fbo.getTexture();
}

bool ShaderPass::isAllocated() const {
    return allocated && fbo.isAllocated();
}

void ShaderPass::clear() {
    fbo.clear();
    allocated = false;
    if (shader.isLoaded()) {
        shader.unload();
    }
}

