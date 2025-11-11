#include "ShaderPass.h"
#include "Shader.h"
#include "../var/Variable.h"
#include <GL/gl.h>
#include "../utils.h"

ShaderPass::ShaderPass(Shader* shader) 
    : shader(shader), allocated(false), outputSize(0, 0, 0), isArrayTexture(false) {
    // FBO will be allocated in update() when we know the actual size
    allocated = false;
}

ShaderPass::~ShaderPass() {
    clear();
}

void ShaderPass::update(ofTexture& inputTexture, TexData& texData) {
    if (shader == nullptr || !shader->isShaderLoaded()) {
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

    // Get sizeMult from shader vars (default to 1.0 if not set)
    glm::vec3 sizeMult(1.0f, 1.0f, 1.0f);
    if (shader->hasVar("sizeMult")) {
        // Try different types for sizeMult
        const Variable<float>* floatVar = dynamic_cast<const Variable<float>*>(shader->getVariable("sizeMult").get());
        const Variable<int>* intVar = dynamic_cast<const Variable<int>*>(shader->getVariable("sizeMult").get());
        const Variable<glm::vec2>* vec2Var = dynamic_cast<const Variable<glm::vec2>*>(shader->getVariable("sizeMult").get());
        const Variable<glm::vec3>* vec3Var = dynamic_cast<const Variable<glm::vec3>*>(shader->getVariable("sizeMult").get());
        
        if (floatVar != nullptr) {
            float mult = floatVar->get();
            sizeMult = glm::vec3(mult, mult, mult);
        }
        else if (intVar != nullptr) {
            int mult = intVar->get();
            sizeMult = glm::vec3(mult, mult, mult);
        }
        else if (vec2Var != nullptr) {
            glm::vec2 mult = vec2Var->get();
            sizeMult = glm::vec3(mult.x, mult.y, 1.0f);
        }
        else if (vec3Var != nullptr) {
            sizeMult = vec3Var->get();
        }
    }

    // Calculate output size: currentSize * sizeMult
    float finalWidth = currentSize.x * sizeMult.x;
    float finalHeight = currentSize.y * sizeMult.y;
    float finalDepth = inputIsArray ? (inputDepth * sizeMult.z) : (currentSize.z * sizeMult.z);

    // Update outputSize with final values
    outputSize = glm::vec3(finalWidth, finalHeight, finalDepth);

    // Update TexData if dimensions changed
#if ALLOW_TEX_2D_ARRAY
    if (currentSize.x != outputSize.x || currentSize.y != outputSize.y || currentSize.z != outputSize.z) {
        texData.setSize(outputSize.x, outputSize.y, outputSize.z);
    }
#else
    if (currentSize.x != outputSize.x || currentSize.y != outputSize.y) {
        texData.setSize(outputSize.x, outputSize.y);
    }
#endif

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
            shader->begin(texData);

            glm::vec2 size = glm::vec2(outputSize.x, outputSize.y);
            shader->setUniform2f("resolution", size.x, size.y);

            shader->setUniformTextureWithSize("srctex", inputTexture);
            shader->setUniform1i("texIndex", layer);  // Pass layer index for shader to use

            getQuad(inputTexture, 0, 0, outputSize.x, outputSize.y).draw();

            shader->end();
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

        glm::vec2 size = glm::vec2(outputSize.x, outputSize.y);
        shader->setUniform2f("resolution", size.x, size.y);

        shader->begin(texData);
        shader->setUniformTextureWithSize("srctex", inputTexture);

        getQuad(inputTexture, 0, 0, outputSize.x, outputSize.y).draw();

        shader->end();
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
    shader = nullptr;
}
