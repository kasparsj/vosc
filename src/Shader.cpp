#include "Shader.h"
#include "Layer.h"
#include "VariablePool.h"
#include "Lights.h"
#include <regex>
#include <exception>
#include "Camera.hpp"

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
        success = loadFromSource(path);
        if (!success) {
            ofLogError() << ("could not load shader from source");
        }
    }
    else if (shadertoyId != "") {
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
        success = loadFromFile(path);
        if (!success) {
            ofLogError() << ("could not load shader: " + path);
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

bool Shader::loadFromFile(const string& path) {
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
        string vertPath = "shaders/passthru.vert";
        string geomPath = "";
        string pathNoExt = fragPath.substr(0, fragPath.find(".frag"));
        string tmpVertPath = pathNoExt + ".vert";
        if (ofFile(tmpVertPath).exists()) {
            vertPath = tmpVertPath;
        }
        string tmpGeomPath = pathNoExt + ".geom";
        if (ofFile(tmpGeomPath).exists()) {
            geomPath = tmpGeomPath;
        }
        ofxAutoReloadedShader* autoShader = new ofxAutoReloadedShader();
        if (autoShader->load(vertPath, fragPath, geomPath)) {
            shader = shared_ptr<ofShader>(autoShader);
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
    else if (command == "/shader/texture") {
        setTexture(m);
    }
    else if (command == "/shader/buffer") {
        setBuffer(m);
    }
    else if (command == "/shader/reset") {
        reset();
    }
}

void Shader::begin(TexData& data, int delay) {
    if (isLoaded()) {
        glm::vec2 size = data.getSize();
        if (shadertoy == NULL) {
            shader->begin();
            shader->setUniform1f("time", data.time);
            shader->setUniform2f("resolution", size.x, size.y);
            shader->setUniform1i("random", data.randomSeed);
            // todo: maybe set only for Layer shaders?
            Camera& cam = Camera::get();
            if (cam.isEnabled()) {
                setUniformCameraMatrices(shader, cam.getCamera());
            }
        }
        else {
            shadertoy->begin();
            shadertoy->setUniform1f("time", data.time);
            shadertoy->setUniform2f("resolution", size.x, size.y);
            shadertoy->setUniform1i("random", data.randomSeed);
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
    if (shadertoy == NULL) {
        for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
            shared_ptr<Texture> tex = it->second;
            if (tex->hasTexture(delay)) {
                for (int i=0; i<tex->getNumTextures(); i++) {
                    string name = i == 0 ? it->first : it->first + ofToString(i);
                    shader->setUniformTexture(name, tex->getTexture(delay, i), texLoc++);
                }
            }
        }
        for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
            shader->setUniformTexture(it->first, it->second->getTexture(), texLoc++);
        }
    }
    else {
        for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
            shared_ptr<Texture> tex = it->second;
            if (tex->hasTexture(delay)) {
                for (int i=0; i<tex->getNumTextures(); i++) {
                    string name = i == 0 ? it->first : it->first + ofToString(i);
                    shadertoy->setUniformTexture(name, tex->getTexture(delay, i), texLoc++);
                }
            }
        }
        for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
            shadertoy->setUniformTexture(it->first, it->second->getTexture(), texLoc++);
        }
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
void Shader::setUniforms(shared_ptr<T>& shader, const map<string, shared_ptr<BaseVar>>& vars) {
    for (map<string, shared_ptr<BaseVar>>::const_iterator it=vars.begin(); it!=vars.end(); ++it) {
        const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(it->second);
        if (floatVar != NULL) {
            const auto& values = floatVar->getVec();
            if (values.size() == 1) {
                shader->setUniform1f(it->first, values[0]);
            }
            else if (values.size() == 2) {
                shader->setUniform2f(it->first, values[0], values[1]);
            }
            else if (values.size() == 3) {
                shader->setUniform3f(it->first, values[0], values[1], values[2]);
            }
            else if (values.size() == 4) {
                shader->setUniform4f(it->first, values[0], values[1], values[2], values[3]);
            }
            else {
                shader->setUniform1fv(it->first, &values[0], values.size());
            }
        }
        else {
            const auto vec3Var = std::dynamic_pointer_cast<const Variable<glm::vec3>>(it->second);
            if (vec3Var != NULL/* && vec3Var->size() == 1*/) {
                shader->setUniform3f(it->first, vec3Var->get());
            }
            else {
                const auto mat4Var = std::dynamic_pointer_cast<const Variable<glm::mat4>>(it->second);
                if (mat4Var != NULL/* && mat4Var->size() == 1*/) {
                    shader->setUniformMatrix4f(it->first, mat4Var->get());
                }
                else {
                    const auto colorVar = std::dynamic_pointer_cast<const Variable<ofFloatColor>>(it->second);
                    if (colorVar != NULL/* && colorVar->size() == 1*/) {
                        shader->setUniform4f(it->first, colorVar->get());
                    }
                    else {
                        ofLogError() << "Shader::setUniforms only float vars implemented";
                    }
                }
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
}

void Shader::unload() {
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

template void Shader::setUniforms(shared_ptr<ofShader>& shader, const map<string, shared_ptr<BaseVar>>& vars);
template void Shader::setUniforms(shared_ptr<ofxShadertoy>& shader, const map<string, shared_ptr<BaseVar>>& vars);
template void Shader::setLights(shared_ptr<ofShader>& shader);
template void Shader::setLights(shared_ptr<ofxShadertoy>& shader);
