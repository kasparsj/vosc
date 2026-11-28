#pragma once

#include "ofMain.h"
#include "../Config.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"
#include "../Texture.h"
#include "../Buffer.hpp"
#include "../TexturePool.h"
#include "../FboPingPong.h"
#include "ofxShadertoy.h"
#include <cstdint>
#include <variant>

#define DEFAULT_TEX "tex"
#define MAX_LIGHTS 8

struct ShaderPaths {
    string vertPath;
    string fragPath;
    string geomPath;
    
    ShaderPaths() : vertPath(""), fragPath(""), geomPath("") {}
    bool isValid() const { return fragPath != ""; }
};

class Camera;

class Shader : public VarsHolder {
    struct SelfFeedback {
        FboPingPong buffers;
        uint64_t step = 0;
        uint64_t drawFrame = UINT64_MAX;
    };
public:
    static constexpr int selfFeedbackWidth = 640;
    static constexpr int selfFeedbackHeight = 480;
    struct DirectSourceState {
        shared_ptr<ofShader> program;
        string path;
        string source;
        map<string, vector<float>> bindings;
    };

    struct UniformInfo {
        GLenum type;
        GLint size;
        bool indexed;
        GLint location;

        bool isArray() const { return indexed || size != 1; }
    };
    using Uniforms = map<string, UniformInfo>;

    struct PreparedDirectSource {
        shared_ptr<ofShader> program;
        string source;
        Uniforms uniforms;
        std::unique_ptr<SelfFeedback> selfFeedback;
    };

    struct DraftProgram {
        PreparedDirectSource source;
        string path = "source";
        map<string, shared_ptr<BaseVar>> variables;
        map<string, shared_ptr<BaseVar>> pool;
    };
    void swapDraftProgram(DraftProgram& draft);

    static map<string, ofxAutoReloadedShader>& shaderCache();
    static string random();
    static bool isRendererUniform(const string& name);
    static const char* sceneUniformError(const Uniforms& uniforms, const string& name);
    static std::string sceneTextureError(const Uniforms& uniforms, const string& name,
                                         bool explicitSize);
    shared_ptr<Texture> loadSceneTexture(const string& name, const string& path, bool feedback,
                                         const glm::vec2& defaultSize);
    
    Shader();
    ~Shader();
    bool isLoaded() const {
        return isShaderLoaded() || shadertoy != NULL;
    }
    bool isShaderLoaded() const {
        return shader != NULL && shader->isLoaded();
    }
    bool load(string path);
    bool loadFromSource(const string& source, bool allowSelfFeedback = false);
    bool snapshotDirectSource(DirectSourceState& state, string& reason) const;
    static bool prepareDirectSource(const string& source, PreparedDirectSource& prepared,
                                    bool allowSelfFeedback = false);
    bool hasSelfFeedback() const { return bool(selfFeedback); }
    uint64_t selfFeedbackStep() const { return selfFeedback ? selfFeedback->step : 0; }
    bool drewSelfFeedbackThisFrame() const {
        return selfFeedback && selfFeedback->drawFrame == uint64_t(ofGetFrameNum());
    }
    void drawSelfFeedback(TexData& data, const glm::vec3& pos, const glm::vec2& size, int layer);
    void installDirectSource(PreparedDirectSource&& prepared,
                             const map<string, vector<float>>& bindings);
    bool restoreDirectSource(const DirectSourceState& state);
    bool ownsProgram(GLuint program) const;
#if VOSC_OPERATOR_UI
    std::weak_ptr<const ofShader> operatorProgram() const { return shader; }
    std::weak_ptr<const void> operatorBackend() const {
        if (shadertoy) return shadertoy;
        return shader;
    }
    std::string operatorSourcePath() const;
    GLuint operatorLinkedProgram() const {
        if (!isShaderLoaded()) return 0;
        return shader->getProgram();
    }
#endif
    bool loadShadertoy(const string& shadertoyId);
    static ShaderPaths getShaderPaths(const string& path);  // Returns vert, frag, and geom paths
    bool loadNativeStages(const ShaderPaths& paths);
    bool loadFromFileAndWatch(const string& path);  // Instance method that loads into this shader
    void reload();
    void update(const vector<TidalNote> &notes);
    void oscCommand(const string& command, const ofxOscMessage& m);
    using IntegerComponent = std::variant<std::int32_t, string>;
    using FloatComponent = std::variant<float, string>;
    size_t reflectedIntegerVectorArity(const string& name) const;
    size_t reflectedFloatVectorArity(const string& name) const;
    void setFloatVector(const string& name, size_t arity, const vector<FloatComponent>& components);
    bool handleLayerVectorWrite(const ofxOscMessage& message, int layerIndex);
    void setIntegerVector(const string& name, size_t arity, const vector<IntegerComponent>& components);
    void begin(TexData& data, int delay = 0, const Camera* camera = nullptr);
    void end();
    void reset();
    void unload();
    const map<string, shared_ptr<Texture>>& getTextures() const {
        return textures;
    }
    const map<string, shared_ptr<Buffer>>& getBuffers() const {
        return buffers;
    }
    bool hasTexture(string name) {
        return textures.find(name) != textures.end();
    }
    shared_ptr<Texture>& getTexture(string name) {
        return textures[name];
    }
    bool hasDefaultTexture() const {
        const auto texture = getDefaultTexture();
        return texture && texture->isLoaded();
    }
    shared_ptr<Texture> getDefaultTexture() const;
    void setDefaultTexture(shared_ptr<Texture>& tex);
    void setTexture(const ofxOscMessage& m);
    void setTexture(const string& name, const ofxOscMessage& m, int arg = 1);
    void setBuffer(const ofxOscMessage& m);
    void setBuffer(const string& name, const ofxOscMessage& m, int arg = 1);
    void setUniform1i(const string& name, int v1);
    void setUniform2f(const string& name, float v1, float v2);
    void setUniformTexture(const string& name, ofTexture& tex, int loc = -1);
    void setUniformTextureWithSize(const string& name, ofTexture& tex, int loc = -1);
    int getNextTextureLocation();
    void setUniformMaterial(ofMaterial& mat, const string& prefix = "mat");
    void set(const ofxOscMessage& m);
    ofShader& getShader() {
        return *shader;
    }
    const ofShader& getShader() const {
        return *shader;
    }
    ofxShadertoy& getShadertoy() {
        return *shadertoy;
    }
    string getShaderPath() const {
        return shaderPath;
    }
    const string& getDirectSource() const { return directSource; }
    bool getExportSource(string& source, string& reason) const;
    const Uniforms& getUniforms() const;
    map<string, string> getUniformValues(bool preciseFloats = false) const;
    
protected:
    bool hasWatchedShader() const { return static_cast<bool>(fileWatch); }
    void setShaderWatchEnabled(bool enabled);

private:
    void clearUniforms();
    GLenum reflectedNonArrayUniformType(const string& name) const;
    void setIntegerVector(const string& name, size_t arity, const vector<IntegerComponent>& components,
                          const shared_ptr<Variable<int>>& variable);
    void publishUniformBinding(const string& name, const shared_ptr<BaseVar>& variable);
    void setUniformTextures(const map<string, shared_ptr<Texture>>& textures, int delay = 0);
    void setUniforms(const map<string, shared_ptr<BaseVar>>& vars);
    template<typename T>
    void setUniforms(shared_ptr<T>& shader, const map<string, shared_ptr<BaseVar>>& vars);
    template<typename T>
    void setLights(shared_ptr<T>& shader);
    template<typename T>
    void setUniformCameraMatrices(shared_ptr<T>& shader, const ofCamera& cam, const string& prefix = "cam");
    template<typename T>
    void setUniformMaterial(shared_ptr<T>& shader, ofMaterial& mat, const string& prefix = "mat");
    
    template<typename T>
    void setUniformFloatArray(shared_ptr<T>& shader, const string& name, const vector<float>& values);
    template<typename T>
    void setUniformIntArray(shared_ptr<T>& shader, const string& name, const vector<int>& values);
    template<typename T>
    void setUniformFromFloatVar(shared_ptr<T>& shader, const string& name, const Variable<float>* var, GLenum uniformType);
    template<typename T>
    void setUniformFromIntVar(shared_ptr<T>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
    template<typename T>
    void setUniformFromFloatFromInt(shared_ptr<T>& shader, const string& name, const Variable<int>* var, GLenum uniformType);

    map<string, shared_ptr<Texture>> textures;
    map<string, shared_ptr<Buffer>> buffers;
    shared_ptr<ofShader> shader;
    std::unique_ptr<SelfFeedback> selfFeedback;
    shared_ptr<ofxShadertoy> shadertoy;
    string shaderPath;
    string directSource;
    struct FileWatch;
    std::unique_ptr<FileWatch> fileWatch;
    mutable Uniforms uniforms;
    mutable GLuint uniformProgram = 0;
    int texLoc;
};
