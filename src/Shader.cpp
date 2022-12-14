#include "Shader.h"
#include "Layer.h"
#include "VariablePool.h"
#include <regex>

void loadShaders(string path, map<string, ofxAutoReloadedShader>& shaders) {
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

bool isSource(string path) {
    return path.find("void main(") != string::npos;
}

string parseShadertoy(string path) {
    std::regex re_view("^https?://(www\\.)?shadertoy\\.com/view/(.+)(/|\\?.*)?");
    std::regex re_api("^https?://(www\\.)?shadertoy\\.com/api/v1/shaders/(.+)(/|\\?.*)");
    std::smatch m;
    if (regex_match(path, m, re_view) && m.size() >= 3) {
        ssub_match submatch = m[2];
        return submatch.str();
    }
    else if (regex_match(path, m, re_api) && m.size() >= 3) {
        ssub_match submatch = m[2];
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
    string fragPath = "";
    if (_isSource) {
        ofShaderSettings settings;
        settings.shaderFiles[GL_VERTEX_SHADER] = "shaders/passthru.vert";
        settings.shaderSources[GL_FRAGMENT_SHADER] = path;
        shader = new ofShader();
        success = shader->setup(settings);
    }
    else if (shadertoyId != "") {
        ofHttpResponse resp = ofLoadURL("https://www.shadertoy.com/api/v1/shaders/" + shadertoyId + "?key=ft8tMh");
        if (resp.status == 200) {
            try {
                ofJson json = ofJson::parse(resp.data.getText());
                string fragPath = "shaders/shadertoy/" + shadertoyId + ".frag";
                ofFile shaderFile;
                shaderFile.open(fragPath, ofFile::WriteOnly);
                shaderFile << json["Shader"]["renderpass"][0]["code"].get<string>();
                shaderFile.close();
                shadertoy = new ofxShadertoy();
                shadertoy->setUseAutoUpdate(true);
                success = shadertoy->load(fragPath);
            }
            catch (exception& ex) {
                ofLogError() << ("could not load shadertoy: " + path + "(" + ex.what() + ")");
            }
        }
    }
    else {
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
            shader = new ofxAutoReloadedShader();
            success = shader->load(vertPath, fragPath, geomPath);
        }
    }
    if (!success) {
        if (_isFile && fragPath == "") {
            ofLogError() << ("could not find shader: " + path);
        }
        
    }
    return success;
}

void Shader::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
}

void Shader::begin(TexData& data, int delay) {
    if (isLoaded()) {
        if (shadertoy == NULL) {
            shader->begin();
            shader->setUniform1f("time", data.time);
            shader->setUniform2f("resolution", data.size.x, data.size.y);
            shader->setUniform1i("random", data.randomSeed);
        }
        else {
            shadertoy->begin();
            shadertoy->setUniform1f("time", data.time);
            shadertoy->setUniform2f("resolution", data.size.x, data.size.y);
            shadertoy->setUniform1i("random", data.randomSeed);
        }
        setUniformTextures(textures, delay);
        setUniforms(vars);
        // todo: do we really need layer vars to override shader vars?
        setUniforms(data.getVars());
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

void Shader::setUniformTextures(const map<string, Texture*>& textures, int delay) {
    int texLoc = 0;
    if (shadertoy == NULL) {
        for (map<string, Texture*>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
            if (it->second->hasTexture(delay)) {
                shader->setUniformTexture(it->first, it->second->getTexture(delay), texLoc++);
            }
        }
    }
    else {
        for (map<string, Texture*>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
            if (it->second->hasTexture(delay)) {
                shadertoy->setUniformTexture(it->first, it->second->getTexture(delay), texLoc++);
            }
        }
    }
}

void Shader::setUniforms(const map<string, BaseVar*>& vars) {
    if (shadertoy == NULL) {
        setUniforms(shader, vars);
    }
    else {
        setUniforms(shadertoy, vars);
    }
}

template<typename T>
void Shader::setUniforms(T* shader, const map<string, BaseVar*>& vars) {
    for (map<string, BaseVar*>::const_iterator it=vars.begin(); it!=vars.end(); ++it) {
        const Variable<float>* floatVar = dynamic_cast<const Variable<float>*>(it->second);
        if (floatVar != NULL) {
            vector<float> values = floatVar->getVec();
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
        }
        else {
            const Variable<glm::vec3>* vec3Var = dynamic_cast<const Variable<glm::vec3>*>(it->second);
            if (vec3Var != NULL/* && vec3Var->size() == 1*/) {
                shader->setUniform3f(it->first, vec3Var->get());
            }
            else {
                const Variable<glm::mat4>* mat4Var = dynamic_cast<const Variable<glm::mat4>*>(it->second);
                if (mat4Var != NULL/* && mat4Var->size() == 1*/) {
                    shader->setUniformMatrix4f(it->first, mat4Var->get());
                }
                else {
                    const Variable<ofFloatColor>* colorVar = dynamic_cast<const Variable<ofFloatColor>*>(it->second);
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

template void Shader::setUniforms(ofShader* shader, const map<string, BaseVar*>& vars);
template void Shader::setUniforms(ofxShadertoy* shader, const map<string, BaseVar*>& vars);

void Shader::reset() {
    unload();
    textures.clear();
    vars.clear();
    TexturePool::clean(_id);
    VariablePool::cleanup(this);
}

void Shader::unload() {
    if (shader != NULL) {
        if (shader->isLoaded()) {
            shader->unload();
        }
        delete shader;
        shader = NULL;
    }
    else if (shadertoy != NULL) {
//        if (shadertoy->isLoaded()) {
//            shadertoy->unload();
//        }
        delete shadertoy;
        shadertoy = NULL;
    }
}

Texture* Shader::getDefaultTexture() {
    return getTexture(DEFAULT_TEX);
}

void Shader::setDefaultTexture(Texture* tex) {
    textures[DEFAULT_TEX] = tex;
}

void Shader::setTexture(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    setTexture(name, m, 2);
}

void Shader::setTexture(string name, const ofxOscMessage& m, int arg) {
    if (textures.find(name) != textures.end()) {
        delete textures[name];
        textures.erase(name);
    }
    if (TexturePool::hasShared(name)) {
        textures[name] = TexturePool::getShared(name);
    }
    else {
        textures[name] = TexturePool::getForShader(name, _id);
        textures[name]->load(m, arg);
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
