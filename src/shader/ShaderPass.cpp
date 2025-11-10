#include "ShaderPass.h"
#include "Shader.h"
#include <GL/gl.h>
#include "../utils.h"

ShaderPass::ShaderPass() : allocated(false), outputSize(0, 0, 0), widthSet(false), heightSet(false), depthSet(false), isArrayTexture(false) {
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

    // Load shader using Shader::getShaderPaths to find all shader paths
    ShaderPaths paths = Shader::getShaderPaths(shaderPath);
    if (!paths.isValid()) {
        ofLogError("ShaderPass") << "Could not find shader file: " << shaderPath;
        return;
    }

    // Load shader
    bool loaded = shader.load(paths.vertPath, paths.fragPath, paths.geomPath);
    if (!loaded) {
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

    // Check if input texture is an array texture
    int inputDepth = inputTexture.getDepth();
    bool inputIsArray = (inputDepth > 1);

    // Inherit dimensions from data if not set
    float finalWidth = widthSet ? outputSize.x : currentSize.x;
    float finalHeight = heightSet ? outputSize.y : currentSize.y;
    float finalDepth = depthSet ? outputSize.z : (inputIsArray ? inputDepth : currentSize.z);

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

    // Handle array textures
    if (inputIsArray && outputSize.z > 1) {
        isArrayTexture = true;

        // Check if array texture needs to be allocated or reallocated
        if (!arrayTexture.isAllocated() ||
            arrayTexture.getWidth() != outputSize.x ||
            arrayTexture.getHeight() != outputSize.y ||
            arrayTexture.getDepth() != outputSize.z) {

            // Allocate array texture
            ofTextureData arrayTexData;
            arrayTexData.width = arrayTexData.tex_w = outputSize.x;
            arrayTexData.height = arrayTexData.tex_h = outputSize.y;
            arrayTexData.depth = arrayTexData.tex_d = outputSize.z;
            arrayTexData.textureTarget = GL_TEXTURE_2D_ARRAY;
            arrayTexData.glInternalFormat = inputTexture.getTextureData().glInternalFormat;
            arrayTexData.minFilter = inputTexture.getTextureData().minFilter;
            arrayTexData.magFilter = inputTexture.getTextureData().magFilter;

            arrayTexture.clear();
            ofDisableTextureEdgeHack();
            arrayTexture.allocate(arrayTexData);
            ofEnableTextureEdgeHack();
        }

        // Allocate FBO if needed (reuse for each layer)
        if (!allocated || fbo.getWidth() != outputSize.x || fbo.getHeight() != outputSize.y) {
            if (allocated) {
                fbo.clear();
            }

            ofFbo::Settings settings;
            settings.width = outputSize.x;
            settings.height = outputSize.y;
            settings.internalformat = GL_RGBA;
            settings.numColorbuffers = 1;
            settings.numSamples = 0;
            settings.useDepth = false;
            settings.useStencil = false;

            fbo.allocate(settings);
            allocated = true;
        }

        // Render each layer separately
        for (int layer = 0; layer < outputSize.z; layer++) {
            fbo.begin();

            // Bind FBO and attach specific layer of output array texture
            GLint attachmentPoint = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, fbo.getId());
            glFramebufferTextureLayer(GL_FRAMEBUFFER,
                                      GL_COLOR_ATTACHMENT0 + attachmentPoint,
                                      (GLuint)arrayTexture.getTextureData().textureID,
                                      0,  // mip level
                                      layer);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                ofLogError("ShaderPass") << "FBO incomplete for layer " << layer << ": 0x" << std::hex << status;
            }

            ofClear(0, 0, 0, 0);
            shader.begin();

            // Set up standard uniforms
            glm::vec2 size = glm::vec2(outputSize.x, outputSize.y);
            shader.setUniform1f("time", texData.time);
            shader.setUniform2f("resolution", size.x, size.y);
            shader.setUniform1i("random", texData.randomSeed);

            // Set input texture as uniform
            // For array textures, the shader should use sampler2DArray and sample with layer index
            shader.setUniformTexture("tex", GL_TEXTURE_2D_ARRAY, inputTexture.getTextureData().textureID, 0);
            shader.setUniform1i("texIndex", layer);  // Pass layer index for shader to use

            getQuad(inputTexture, 0, 0, outputSize.x, outputSize.y).draw();

            shader.end();
            fbo.end();
        }
    }
    else {
        // Regular 2D texture handling
        isArrayTexture = false;

        // Check if FBO needs to be allocated or reallocated
        if (!allocated || fbo.getWidth() != outputSize.x || fbo.getHeight() != outputSize.y) {
            if (allocated) {
                fbo.clear();
            }

            // Allocate FBO
            ofFbo::Settings settings;
            settings.width = outputSize.x;
            settings.height = outputSize.y;
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

        getQuad(inputTexture, 0, 0, outputSize.x, outputSize.y).draw();

        shader.end();
        fbo.end();
    }
}

ofTexture& ShaderPass::getTexture() {
    if (isArrayTexture) {
        return arrayTexture;
    }
    return fbo.getTexture();
}

bool ShaderPass::isAllocated() const {
    if (isArrayTexture) {
        return arrayTexture.isAllocated();
    }
    return allocated && fbo.isAllocated();
}

void ShaderPass::clear() {
    fbo.clear();
    arrayTexture.clear();
    allocated = false;
    isArrayTexture = false;
    if (shader.isLoaded()) {
        shader.unload();
    }
}

