#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <set>
#include <cstdint>

class Texture;
class Geom;
class Shader;
class BaseVar;
class Inputs;
class Lights;
class ShaderPingPongTex;

class ResourceRegistry {
public:
    class Inactive : public std::logic_error {
    public:
        using std::logic_error::logic_error;
    };

    using TextureSharedMap = std::map<std::string, std::shared_ptr<Texture>>;
    using TextureShaderMap = std::map<int, std::map<std::string, std::shared_ptr<Texture>>>;
    using GeomSharedMap = std::map<std::string, std::shared_ptr<Geom>>;
    using GeomLayerMap = std::map<int, std::shared_ptr<Geom>>;
    using ShaderSharedMap = std::map<std::string, std::shared_ptr<Shader>>;
    using VariableSharedMap = std::map<std::string, std::shared_ptr<BaseVar>>;
    using VariableHolderMap = std::map<int, std::map<std::string, std::shared_ptr<BaseVar>>>;

    struct FeedbackState {
        uint64_t generation = 1;
        std::set<ShaderPingPongTex*> instances;
    };

    class Scope {
    public:
        explicit Scope(ResourceRegistry& active);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        ResourceRegistry* previous;
    };

    ResourceRegistry();
    ~ResourceRegistry();

    void activate();
    void deactivate();

    static ResourceRegistry* current();
    static bool hasCurrent();

    void clear();

    TextureSharedMap textureSharedPool;
    TextureShaderMap textureShaderPool;
    GeomSharedMap geomSharedPool;
    GeomLayerMap geomLayerPool;
    ShaderSharedMap shaderSharedPool;
    VariableSharedMap variableSharedPool;
    VariableHolderMap variableHolderPool;
    bool variablePoolShuttingDown = false;
    std::unique_ptr<Inputs> inputs;
    std::unique_ptr<Lights> lights;
    std::shared_ptr<FeedbackState> feedback;

private:
    static ResourceRegistry* currentRegistry;
};
