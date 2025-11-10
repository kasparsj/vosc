#include "Shader.h"
#include "../layer/Layer.h"
#include "../var/VariablePool.h"
#include "../light/Lights.h"
#include <regex>
#include <exception>
#include <set>
#include "../Camera.hpp"
#include "../utils.h"

void loadShaders(const string& path, map<string, ofxAutoReloadedShader>& shaders) {
    ofDirectory dir(path);
    for (int i = 0; i < dir.getFiles().size(); i++) {
        ofFile file = dir.getFile(i);
        string relPath = path + "/" + file.getFileName();
        if (file.isDirectory() && relPath != "shaders/common") {
            loadShaders(path + "/" + file.getFileName(), shaders);
        }
        else {
            ofxAutoReloadedShader sh;
            string path = file.getAbsolutePath();
            string pathNoExt = path.substr(0, path.find("." + file.getExtension()));
            shaders[pathNoExt] = sh;
        }
    }
}

map<string, ofxAutoReloadedShader> loadShaderCache() {
    map<string, ofxAutoReloadedShader> shaders;
    loadShaders("shaders", shaders);
    return shaders;
}

map<string, ofxAutoReloadedShader> Shader::cache = loadShaderCache();

string Shader::random() {
    auto it = cache.begin();
    advance(it, int(ofRandom(cache.size())));
    return it->first;
}

bool isSource(const string& path) {
    return path.find("void main(") != string::npos;
}

string parseShadertoy(const string& path) {
    std::regex re_view("^https?://(www\\.)?shadertoy\\.com/view/(.+)(/|\\?.*)?");
    std::regex re_api("^https?://(www\\.)?shadertoy\\.com/api/v1/shaders/(.+)(/|\\?.*)");
    std::smatch m;
    if (regex_match(path, m, re_view) && m.size() >= 3) {
        std::ssub_match submatch = m[2];
        return submatch.str();
    }
    else if (regex_match(path, m, re_api) && m.size() >= 3) {
        std::ssub_match submatch = m[2];
        return submatch.str();
    }
    return "";
}

bool Shader::load(string path) {
    unload();
    bool success = false;
    bool _isSource = isSource(path);
    string shadertoyId = parseShadertoy(path);
    bool _isFile = !_isSource && shadertoyId == "";
    if (_isSource) {
        shaderPath = "source";
        success = loadFromSource(path);
        if (!success) {
            ofLogError() << ("could not load shader from source");
        }
    }
    else if (shadertoyId != "") {
        shaderPath = "shadertoy/" + shadertoyId + ".frag";
        try {
            success = loadShadertoy(shadertoyId);
            if (!success) {
                ofLogError() << ("could not load shadertoy: " + shadertoyId);
            }
        }
        catch (std::exception& ex) {
            ofLogError() << ("could not load shadertoy: " + path + "(" + ex.what() + ")");
        }
    }
    else if (path.empty() == false) {
        shaderPath = path;
        success = loadFromFileAndWatch(path);
        if (!success) {
            ofLogError() << ("could not load shader: " + path);
            shaderPath = "";
        }
    }
    return success;
}

bool Shader::loadFromSource(const string& source) {
    ofShaderSettings settings;
    settings.shaderFiles[GL_VERTEX_SHADER] = "shaders/passthru.vert";
    settings.shaderSources[GL_FRAGMENT_SHADER] = source;
    shader = make_shared<ofShader>();
    return shader->setup(settings);
}

bool Shader::loadShadertoy(const string& shadertoyId) {
    ofHttpResponse resp = ofLoadURL("https://www.shadertoy.com/api/v1/shaders/" + shadertoyId + "?key=ft8tMh");
    if (resp.status == 200) {
        ofJson json = ofJson::parse(resp.data.getText());
        string fragPath = "shaders/shadertoy/" + shadertoyId + ".frag";
        ofFile shaderFile;
        shaderFile.open(fragPath, ofFile::WriteOnly);
        shaderFile << json["Shader"]["renderpass"][0]["code"].get<string>();
        shaderFile.close();
        shadertoy = make_shared<ofxShadertoy>();
        shadertoy->setUseAutoUpdate(true);
        return shadertoy->load(fragPath);
    }
    return false;
}

ShaderPaths Shader::getShaderPaths(const string& path) {
    ShaderPaths paths;
    string fragPath = "";
    vector<string> fragPaths;
    if (!ofFilePath::isAbsolute(path)) {
        fragPaths.insert(fragPaths.end(), {
            ofToDataPath(path),
            ofToDataPath(path + ".frag"),
            ofToDataPath("shaders/" + path),
            ofToDataPath("shaders/" + path + ".frag"),
        });
    }
    else {
        fragPaths.insert(fragPaths.end(), {
            path,
            path + ".frag",
        });
    }
    for (int i=0; i<fragPaths.size(); i++) {
        if (ofFile(fragPaths[i]).exists()) {
            fragPath = fragPaths[i];
            break;
        }
    }
    
    if (fragPath != "") {
        paths.fragPath = fragPath;
        paths.vertPath = "shaders/passthru.vert";
        paths.geomPath = "";
        
        string pathNoExt = fragPath.substr(0, fragPath.find(".frag"));
        string tmpVertPath = pathNoExt + ".vert";
        if (ofFile(tmpVertPath).exists()) {
            paths.vertPath = tmpVertPath;
        }
        string tmpGeomPath = pathNoExt + ".geom";
        if (ofFile(tmpGeomPath).exists()) {
            paths.geomPath = tmpGeomPath;
        }
    }
    
    return paths;
}

bool Shader::loadFromFileAndWatch(const string& path) {
    ShaderPaths paths = getShaderPaths(path);
    if (paths.isValid()) {
        ofxAutoReloadedShader* autoShader = new ofxAutoReloadedShader();
        if (autoShader->load(paths.vertPath, paths.fragPath, paths.geomPath)) {
            shader = shared_ptr<ofShader>(autoShader);
            shaderPath = ofFilePath::getFileName(paths.fragPath);
            return true;
        }
    }
    return false;
}

void Shader::update(const vector<TidalNote> &notes) {
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        it->second->update();
    }
}

void Shader::oscCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/shader") {
        load(m.getArgAsString(1));
    }
    else if (command == "/shader/set") {
        set(m);
    }
    else if (command == "/shader/var" || command == "/shader/uniform") {
        string name = m.getArgAsString(1);
        setVar(name, m, 2);
    }
    else if (command == "/shader/texture" || command == "/shader/tex") {
        setTexture(m);
    }
    else if (command == "/shader/buffer" || command == "/shader/buf") {
        setBuffer(m);
    }
    else if (command == "/shader/reset") {
        reset();
    }
}

void Shader::begin(TexData& data, int delay) {
    if (isLoaded()) {
        glm::vec2 size = data.getSize();
        auto beginShader = [&data, &size](auto& sh) {
            sh.begin();
            sh.setUniform1f("time", data.time);
            sh.setUniform2f("resolution", size.x, size.y);
            sh.setUniform1i("random", data.randomSeed);
        };
        
        if (shadertoy == NULL) {
            beginShader(*shader);
            // todo: maybe set only for Layer shaders?
            Camera& cam = Camera::get();
            if (cam.isEnabled()) {
                setUniformCameraMatrices(shader, cam.getCamera());
            }
        }
        else {
            beginShader(*shadertoy);
        }
        setUniformTextures(textures, delay);
        setUniforms(vars);
        setUniforms(data.getVars());
        if (shadertoy == NULL) {
            setLights(shader);
        }
        else {
            setLights(shadertoy);
        }
    }
}

void Shader::end() {
    if (isLoaded()) {
        if (shadertoy == NULL) {
            shader->end();
        }
        else {
            shadertoy->end();
        }
    }
}

void Shader::setUniformTextures(const map<string, shared_ptr<Texture>>& textures, int delay) {
    int texLoc = 0;
    auto setTextures = [&texLoc, delay](auto& shader, const map<string, shared_ptr<Texture>>& texs, const map<string, shared_ptr<Buffer>>& bufs) {
        for (const auto& it : texs) {
            shared_ptr<Texture> tex = it.second;
            if (tex->hasTexture(delay)) {
                for (int i=0; i<tex->getNumTextures(); i++) {
                    string name = i == 0 ? it.first : it.first + ofToString(i);
                    const ofTexture& texture = tex->getTexture(delay, i);
                    shader.setUniformTexture(name, texture, texLoc++);
                    // Set texture size as vec2 uniform
                    shader.setUniform2f(name + "Size", texture.getWidth(), texture.getHeight());
                }
            }
        }
        for (const auto& it : bufs) {
            const ofTexture& texture = it.second->getTexture();
            shader.setUniformTexture(it.first, texture, texLoc++);
            // Set buffer texture size as vec2 uniform
            shader.setUniform2f(it.first + "Size", texture.getWidth(), texture.getHeight());
        }
    };
    
    if (shadertoy == NULL) {
        setTextures(*shader, textures, buffers);
    }
    else {
        setTextures(*shadertoy, textures, buffers);
    }
}

void Shader::setUniforms(const map<string, shared_ptr<BaseVar>>& vars) {
    if (shadertoy == NULL) {
        setUniforms(shader, vars);
    }
    else {
        setUniforms(shadertoy, vars);
    }
}

template<typename T>
void Shader::setUniformFloatArray(shared_ptr<T>& shader, const string& name, const vector<float>& values) {
    if (values.size() == 1) {
        shader->setUniform1f(name, values[0]);
    }
    else if (values.size() == 2) {
        shader->setUniform2f(name, values[0], values[1]);
    }
    else if (values.size() == 3) {
        shader->setUniform3f(name, values[0], values[1], values[2]);
    }
    else if (values.size() == 4) {
        shader->setUniform4f(name, values[0], values[1], values[2], values[3]);
    }
    else {
        shader->setUniform1fv(name, &values[0], values.size());
    }
}

template<typename T>
void Shader::setUniformIntArray(shared_ptr<T>& shader, const string& name, const vector<int>& values) {
    if (values.size() == 1) {
        shader->setUniform1i(name, values[0]);
    }
    else if (values.size() == 2) {
        shader->setUniform2i(name, values[0], values[1]);
    }
    else if (values.size() == 3) {
        shader->setUniform3i(name, values[0], values[1], values[2]);
    }
    else if (values.size() == 4) {
        shader->setUniform4i(name, values[0], values[1], values[2], values[3]);
    }
    else {
        shader->setUniform1iv(name, &values[0], values.size());
    }
}

template<typename T>
void Shader::setUniformFromFloatVar(shared_ptr<T>& shader, const string& name, const Variable<float>* var, GLenum uniformType) {
    if (var == NULL) return;
    const auto& values = var->getVec();
    
    switch (uniformType) {
        case GL_FLOAT_VEC2:
            if (values.size() >= 2) {
                shader->setUniform2f(name, values[0], values[1]);
            }
            break;
        case GL_FLOAT_VEC3:
            if (values.size() >= 3) {
                shader->setUniform3f(name, values[0], values[1], values[2]);
            }
            break;
        case GL_FLOAT_VEC4:
            if (values.size() >= 4) {
                shader->setUniform4f(name, values[0], values[1], values[2], values[3]);
            }
            break;
        default:
            setUniformFloatArray(shader, name, values);
            break;
    }
}

// Helper function to get dimension from GLenum uniform type
static int getUniformTypeDimension(GLenum uniformType) {
    switch (uniformType) {
        case GL_FLOAT_VEC2:
        case GL_INT_VEC2:
            return 2;
        case GL_FLOAT_VEC3:
        case GL_INT_VEC3:
            return 3;
        case GL_FLOAT_VEC4:
        case GL_INT_VEC4:
            return 4;
        default:
            return 0;
    }
}

// Helper function to try setting vec2 uniform from a glm vector variable
// Can use vec2, vec3, or vec4 (takes first 2 components)
template<typename T>
static bool trySetUniformVec2(shared_ptr<T>& shader, const string& name, shared_ptr<const BaseVar> var) {
    const auto vec2Var = std::dynamic_pointer_cast<const Variable<glm::vec2>>(var);
    if (vec2Var != NULL && glmVecTypeDimensionOf(vec2Var->get()) >= 2) {
        glm::vec2 vec = vec2Var->get();
        shader->setUniform2f(name, vec.x, vec.y);
        return true;
    }
    const auto vec3Var = std::dynamic_pointer_cast<const Variable<glm::vec3>>(var);
    if (vec3Var != NULL && glmVecTypeDimensionOf(vec3Var->get()) >= 2) {
        glm::vec3 vec = vec3Var->get();
        shader->setUniform2f(name, vec.x, vec.y);
        return true;
    }
    const auto vec4Var = std::dynamic_pointer_cast<const Variable<glm::vec4>>(var);
    if (vec4Var != NULL && glmVecTypeDimensionOf(vec4Var->get()) >= 2) {
        glm::vec4 vec = vec4Var->get();
        shader->setUniform2f(name, vec.x, vec.y);
        return true;
    }
    return false;
}

// Helper function to try setting vec3 uniform from a glm vector variable
// Can use vec3 or vec4 (takes first 3 components)
template<typename T>
static bool trySetUniformVec3(shared_ptr<T>& shader, const string& name, shared_ptr<const BaseVar> var) {
    const auto vec3Var = std::dynamic_pointer_cast<const Variable<glm::vec3>>(var);
    if (vec3Var != NULL && glmVecTypeDimensionOf(vec3Var->get()) >= 3) {
        shader->setUniform3f(name, vec3Var->get());
        return true;
    }
    const auto vec4Var = std::dynamic_pointer_cast<const Variable<glm::vec4>>(var);
    if (vec4Var != NULL && glmVecTypeDimensionOf(vec4Var->get()) >= 3) {
        glm::vec4 vec = vec4Var->get();
        shader->setUniform3f(name, vec.x, vec.y, vec.z);
        return true;
    }
    const auto colorVar = std::dynamic_pointer_cast<const Variable<ofFloatColor>>(var);
    if (colorVar != NULL) {
        ofFloatColor c = colorVar->get();
        shader->setUniform3f(name, c.r, c.g, c.b);
        return true;
    }
    return false;
}

// Helper function to try setting vec4 uniform from a glm vector variable
// Can only use vec4 or ofFloatColor
template<typename T>
static bool trySetUniformVec4(shared_ptr<T>& shader, const string& name, shared_ptr<const BaseVar> var) {
    const auto vec4Var = std::dynamic_pointer_cast<const Variable<glm::vec4>>(var);
    if (vec4Var != NULL && glmVecTypeDimensionOf(vec4Var->get()) == 4) {
        shader->setUniform4f(name, vec4Var->get());
        return true;
    }
    const auto colorVar = std::dynamic_pointer_cast<const Variable<ofFloatColor>>(var);
    if (colorVar != NULL) {
        shader->setUniform4f(name, colorVar->get());
        return true;
    }
    return false;
}

template<typename T>
void Shader::setUniformFromIntVar(shared_ptr<T>& shader, const string& name, const Variable<int>* var, GLenum uniformType) {
    if (var == NULL) return;
    const auto& values = var->getVec();
    
    switch (uniformType) {
        case GL_INT:
        case GL_INT_VEC2:
        case GL_INT_VEC3:
        case GL_INT_VEC4:
            setUniformIntArray(shader, name, values);
            break;
        default:
            if (values.size() >= 1) {
                shader->setUniform1i(name, values[0]);
            }
            break;
    }
}

template<typename T>
void Shader::setUniforms(shared_ptr<T>& shader, const map<string, shared_ptr<BaseVar>>& vars) {
    const map<string, GLenum>& types = getUniformTypes();

    // Built-in OpenFrameworks uniforms that are set automatically
    static const std::set<string> builtInUniforms = {
        "projectionMatrix",
        "modelViewMatrix",
        "modelViewProjectionMatrix",
        "textureMatrix",
    };
    
    // Loop through uniform types to ensure all shader uniforms are set
    for (const auto& typeIt : types) {
        const string& name = typeIt.first;
        GLenum uniformType = typeIt.second;

        // Check if it's a type we can handle in the switch statement
        bool isHandledType = (uniformType == GL_INT || uniformType == GL_INT_VEC2 || uniformType == GL_INT_VEC3 || uniformType == GL_INT_VEC4 ||
                              uniformType == GL_FLOAT || uniformType == GL_FLOAT_VEC2 || uniformType == GL_FLOAT_VEC3 ||
                              uniformType == GL_FLOAT_VEC4 || uniformType == GL_FLOAT_MAT4);
        if (!isHandledType) {
            continue;
        }
        
        // Find corresponding variable in vars
        auto varIt = vars.find(name);
        if (varIt == vars.end()) {
            continue;
        }
        
        switch (uniformType) {
            case GL_INT:
            case GL_INT_VEC2:
            case GL_INT_VEC3:
            case GL_INT_VEC4: {
                const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                if (intVar != NULL) {
                    setUniformFromIntVar(shader, name, intVar.get(), uniformType);
                }
                else {
                    const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                    if (floatVar != NULL && floatVar->getVec().size() >= 1) {
                        shader->setUniform1i(name, (int)floatVar->getVec()[0]);
                    }
                    else {
                        // todo: log error
                    }
                }
                break;
            }
            case GL_FLOAT_VEC2: {
                if (!trySetUniformVec2(shader, name, varIt->second)) {
                    setUniformFromFloatVar(shader, name, 
                        std::dynamic_pointer_cast<const Variable<float>>(varIt->second).get(), uniformType);
                }
                break;
            }
            case GL_FLOAT_VEC3: {
                if (!trySetUniformVec3(shader, name, varIt->second)) {
                    setUniformFromFloatVar(shader, name, 
                        std::dynamic_pointer_cast<const Variable<float>>(varIt->second).get(), uniformType);
                }
                break;
            }
            case GL_FLOAT_VEC4: {
                if (!trySetUniformVec4(shader, name, varIt->second)) {
                    setUniformFromFloatVar(shader, name, 
                        std::dynamic_pointer_cast<const Variable<float>>(varIt->second).get(), uniformType);
                }
                break;
            }
            case GL_FLOAT_MAT4: {
                const auto mat4Var = std::dynamic_pointer_cast<const Variable<glm::mat4>>(varIt->second);
                if (mat4Var != NULL) {
                    shader->setUniformMatrix4f(name, mat4Var->get());
                }
                break;
            }
            case GL_FLOAT:
            default: {
                const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                if (floatVar != NULL) {
                    setUniformFromFloatVar(shader, name, floatVar.get(), uniformType);
                }
                else {
                    // Try other types as fallback
                    const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                    if (intVar != NULL && intVar->getVec().size() >= 1) {
                        shader->setUniform1f(name, (float)intVar->getVec()[0]);
                    }
                    else {
                        // Try glm vectors as fallback
                        if (trySetUniformVec2(shader, name, varIt->second) ||
                            trySetUniformVec3(shader, name, varIt->second) ||
                            trySetUniformVec4(shader, name, varIt->second)) {
                            // Successfully set from glm vector
                        }
                    }
                }
                break;
            }
        }
    }
}

template<typename T>
void Shader::setLights(shared_ptr<T>& shader) {
    vector<glm::vec3> lightPos;
    const map<string, shared_ptr<Light>>& lights = Lights::get().all();
    for (map<string, shared_ptr<Light>>::const_iterator it=lights.begin(); it!=lights.end(); ++it) {
        lightPos.push_back(it->second->getVarVec3("pos"));
    }
    int numLights = MIN(MAX_LIGHTS, lightPos.size());
    shader->setUniform3fv("lights", &lightPos[0][0], numLights);
    shader->setUniform1i("numLights", numLights);
}

template<typename T>
void Shader::setUniformCameraMatrices(shared_ptr<T>& shader, ofCamera& cam, const string& prefix) {
    shader->setUniformMatrix4f(prefix + "ModelViewMatrix", cam.getModelViewMatrix() );
    shader->setUniformMatrix4f(prefix + "ProjectionMatrix", cam.getProjectionMatrix() );
    shader->setUniformMatrix4f(prefix + "ModelViewProjectionMatrix", cam.getModelViewProjectionMatrix() );
}

void Shader::setUniformMaterial(ofMaterial& mat, const string& prefix) {
    if (shadertoy == NULL) {
        setUniformMaterial(shader, mat, prefix);
    }
    else {
        setUniformMaterial(shadertoy, mat, prefix);
    }
}

template<typename T>
void Shader::setUniformMaterial(shared_ptr<T>& shader, ofMaterial& mat, const string& prefix) {
    shader->setUniform4f(prefix + "Emission", mat.getEmissiveColor());
    shader->setUniform4f(prefix + "Ambient", mat.getAmbientColor());
    shader->setUniform4f(prefix + "Diffuse", mat.getDiffuseColor());
    shader->setUniform4f(prefix + "Specular", mat.getSpecularColor());
    shader->setUniform1f(prefix + "Shininess", mat.getShininess());
}

void Shader::reset() {
    unload();
    TexturePool::clean(_id);
    textures.clear();
    buffers.clear();
    VariablePool::cleanup(_id);
    vars.clear();
    uniformTypes.clear();
}

void Shader::unload() {
    shaderPath = "";
    if (shader != NULL) {
        try {
            shared_ptr<ofxAutoReloadedShader> autoShader = std::static_pointer_cast<ofxAutoReloadedShader>(shader);
            if (autoShader->isLoaded()) {
                autoShader->unload();
            }
        }
        catch (...) {
            if (shader->isLoaded()) {
                shader->unload();
            }
        }
        shader = NULL;
    }
    else if (shadertoy != NULL) {
//        if (shadertoy->isLoaded()) {
//            shadertoy->unload();
//        }
        shadertoy = NULL;
    }
}

shared_ptr<Texture>& Shader::getDefaultTexture() {
    return getTexture(DEFAULT_TEX);
}

void Shader::setDefaultTexture(shared_ptr<Texture>& tex) {
    textures[DEFAULT_TEX] = tex;
}

void Shader::setTexture(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    setTexture(name, m, 2);
}

void Shader::setTexture(const string& name, const ofxOscMessage& m, int arg) {
    if (textures.find(name) != textures.end()) {
        textures.erase(name);
    }
    string source = m.getArgAsString(arg);
    if (TexturePool::hasShared(source)) {
        textures[name] = TexturePool::getShared(source);
    }
    else {
        textures[name] = TexturePool::getOrCreate(name, this);
        textures[name]->load(m, arg);
    }
}

void Shader::setBuffer(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    setBuffer(name, m, 2);
}

void Shader::setBuffer(const string& name, const ofxOscMessage& m, int arg) {
    if (buffers.find(name) != buffers.end()) {
        buffers.erase(name);
    }
    buffers[name] = make_shared<Buffer>(name, m, arg, this);
}

void Shader::setUniform1i(const string& name, int v1) {
    if (shadertoy == NULL) {
        shader->setUniform1i(name, v1);
    }
    else {
        shadertoy->setUniform1i(name, v1);
    }
}

void Shader::setUniform2f(const string& name, float v1, float v2) {
    if (shadertoy == NULL) {
        shader->setUniform2f(name, v1, v2);
    }
    else {
        shadertoy->setUniform2f(name, v1, v2);
    }
}

void Shader::setUniformTexture(const string& name, ofTexture& tex, int loc) {
    if (shadertoy == NULL) {
        shader->setUniformTexture(name, tex, loc);
    }
    else {
        shadertoy->setUniformTexture(name, tex, loc);
    }
}

void Shader::set(const ofxOscMessage& m) {
    if (isShaderLoaded()) {
        string prop = m.getArgAsString(1);
        if (prop == "geomInputType") {
            shader->setGeometryInputType(static_cast<GLenum>(m.getArgAsInt(2)));
        }
        else if (prop == "geomOutputType") {
            shader->setGeometryOutputType(static_cast<GLenum>(m.getArgAsInt(2)));
        }
        else if (prop == "geomOutputCount") {
            shader->setGeometryOutputCount(m.getArgAsInt(2));
        }
    }
}

const map<string, GLenum>& Shader::getUniformTypes() const {
    if (uniformTypes.empty() && isShaderLoaded()) {
        if (shadertoy == NULL && shader != NULL) {
            GLuint program = shader->getProgram();
            if (program != 0) {
                GLint numUniforms = 0;
                glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
                
                GLint maxNameLength = 0;
                glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLength);
                
                if (maxNameLength > 0) {
                    vector<GLchar> nameBuffer(maxNameLength);
                    
                    for (GLint i = 0; i < numUniforms; i++) {
                        GLint size = 0;
                        GLenum type = 0;
                        GLsizei length = 0;
                        
                        glGetActiveUniform(program, i, maxNameLength, &length, &size, &type, nameBuffer.data());
                        
                        if (length > 0) {
                            string uniformName(nameBuffer.data(), length);
                            // Handle array uniforms (e.g., "array[0]" -> "array")
                            size_t bracketPos = uniformName.find('[');
                            if (bracketPos != string::npos) {
                                uniformName = uniformName.substr(0, bracketPos);
                            }
                            uniformTypes[uniformName] = type;
                        }
                    }
                }
            }
        }
        // Note: ofxShadertoy doesn't provide direct access to uniform types
        // For shadertoy, we could leave the map empty or try to infer from usage
    }
    return uniformTypes;
}

map<string, string> Shader::getUniformValues() const {
    map<string, string> values;
    
    if (!isShaderLoaded() || shader == NULL) {
        return values;
    }
    
    GLuint program = shader->getProgram();
    if (program == 0) {
        return values;
    }
    
    const map<string, GLenum>& uniformTypes = getUniformTypes();
    
    for (const auto& it : uniformTypes) {
        const string& name = it.first;
        GLenum type = it.second;
        
        GLint location = glGetUniformLocation(program, name.c_str());
        if (location < 0) {
            continue;
        }
        
        string valueStr;
        
        // Query uniform value based on type
        if (type == GL_FLOAT) {
            float value;
            glGetUniformfv(program, location, &value);
            valueStr = ofToString(value);
        }
        else if (type == GL_INT || type == GL_SAMPLER_2D || type == GL_SAMPLER_3D || 
                 type == GL_SAMPLER_CUBE || type == GL_SAMPLER_BUFFER) {
            int value;
            glGetUniformiv(program, location, &value);
            valueStr = ofToString(value);
        }
        else if (type == GL_FLOAT_VEC2) {
            float values[2];
            glGetUniformfv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + "]";
        }
        else if (type == GL_FLOAT_VEC3) {
            float values[3];
            glGetUniformfv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + "]";
        }
        else if (type == GL_FLOAT_VEC4) {
            float values[4];
            glGetUniformfv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + ", " + ofToString(values[3]) + "]";
        }
        else if (type == GL_INT_VEC2) {
            int values[2];
            glGetUniformiv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + "]";
        }
        else if (type == GL_INT_VEC3) {
            int values[3];
            glGetUniformiv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + "]";
        }
        else if (type == GL_INT_VEC4) {
            int values[4];
            glGetUniformiv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + ", " + ofToString(values[3]) + "]";
        }
        
        if (!valueStr.empty()) {
            values[name] = valueStr;
        }
    }
    
    return values;
}

template void Shader::setUniformFloatArray(shared_ptr<ofShader>& shader, const string& name, const vector<float>& values);
template void Shader::setUniformFloatArray(shared_ptr<ofxShadertoy>& shader, const string& name, const vector<float>& values);
template void Shader::setUniformIntArray(shared_ptr<ofShader>& shader, const string& name, const vector<int>& values);
template void Shader::setUniformIntArray(shared_ptr<ofxShadertoy>& shader, const string& name, const vector<int>& values);
template void Shader::setUniformFromFloatVar(shared_ptr<ofShader>& shader, const string& name, const Variable<float>* var, GLenum uniformType);
template void Shader::setUniformFromFloatVar(shared_ptr<ofxShadertoy>& shader, const string& name, const Variable<float>* var, GLenum uniformType);
template void Shader::setUniformFromIntVar(shared_ptr<ofShader>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
template void Shader::setUniformFromIntVar(shared_ptr<ofxShadertoy>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
template void Shader::setUniforms(shared_ptr<ofShader>& shader, const map<string, shared_ptr<BaseVar>>& vars);
template void Shader::setUniforms(shared_ptr<ofxShadertoy>& shader, const map<string, shared_ptr<BaseVar>>& vars);
template void Shader::setLights(shared_ptr<ofShader>& shader);
template void Shader::setLights(shared_ptr<ofxShadertoy>& shader);
