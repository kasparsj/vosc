#pragma once

#include <map>
#include <memory>
#include <string>

class Texture;
class Geom;
class Shader;
class BaseVar;

class ResourceRegistry {
public:
    using TextureSharedMap = std::map<std::string, std::shared_ptr<Texture>>;
    using TextureShaderMap = std::map<int, std::map<std::string, std::shared_ptr<Texture>>>;
    using GeomSharedMap = std::map<std::string, std::shared_ptr<Geom>>;
    using GeomLayerMap = std::map<int, std::shared_ptr<Geom>>;
    using ShaderSharedMap = std::map<std::string, std::shared_ptr<Shader>>;
    using VariableSharedMap = std::map<std::string, std::shared_ptr<BaseVar>>;
    using VariableHolderMap = std::map<int, std::map<std::string, std::shared_ptr<BaseVar>>>;

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

private:
    static ResourceRegistry* currentRegistry;
};
