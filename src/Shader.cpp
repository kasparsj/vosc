#include "Shader.h"
#include "Layer.h"
#include "VariablePool.h"

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

bool Shader::load(string path) {
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
    string fragPath = "";
    for (int i=0; i<fragPaths.size(); i++) {
        if (ofFile(fragPaths[i]).exists()) {
            fragPath = fragPaths[i];
            break;
        }
    }
    if (fragPath != "") {
        string vertPath = "shaders/passthru.vert";
        string pathNoExt = fragPath.substr(0, fragPath.find(".frag"));
        string tmpVertPath = pathNoExt + ".vert";
        if (ofFile(tmpVertPath).exists()) {
            vertPath = tmpVertPath;
        }
        string geomPath = pathNoExt + ".geom";
        return shader.load(vertPath, fragPath, geomPath);
    }
    ofLog() << "could not find shader " << path;
    return false;
}

void Shader::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
}

void Shader::begin(TexData& data, int delay) {
    if (isLoaded()) {
        shader.begin();
        shader.setUniform1f("time", data.time);
        shader.setUniform2f("resolution", data.size.x, data.size.y);
        // todo: overlaps with Source::COLOR
        shader.setUniform4f("color", data.getVarColor("color"));
        shader.setUniform1i("random", data.randomSeed);
        int texLoc = 0;
        for (map<string, Texture*>::iterator it=textures.begin(); it!=textures.end(); ++it) {
            if (it->second->hasTexture(delay)) {
                shader.setUniformTexture(it->first, it->second->getTexture(delay), texLoc++);
            }
        }
        for (map<string, Variable*>::const_iterator it=data.getVars().begin(); it!=data.getVars().end(); ++it) {
            if (it->second->values.size() == 1) {
                shader.setUniform1f(it->first, it->second->values[0]);
            }
            else if (it->second->values.size() == 2) {
                shader.setUniform2f(it->first, it->second->values[0], it->second->values[1]);
            }
            else if (it->second->values.size() == 3) {
                shader.setUniform3f(it->first, it->second->values[0], it->second->values[1], it->second->values[2]);
            }
            else if (it->second->values.size() == 4) {
                shader.setUniform4f(it->first, it->second->values[0], it->second->values[1], it->second->values[2], it->second->values[3]);
            }
        }
    }
}

void Shader::end() {
    if (isLoaded()) {
        shader.end();
    }
}

void Shader::reset() {
    if (shader.isLoaded()) {
        shader.unload();
    }
    textures.clear();
    vars.clear();
    TexturePool::clean(_id);
    VariablePool::cleanup(this);
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
    string prop = m.getArgAsString(1);
    if (prop == "geomInputType") {
        getShader().setGeometryInputType(static_cast<GLenum>(m.getArgAsInt(2)));
    }
    else if (prop == "geomOutputType") {
        getShader().setGeometryOutputType(static_cast<GLenum>(m.getArgAsInt(2)));
    }
    else if (prop == "geomOutputCount") {
        getShader().setGeometryOutputCount(m.getArgAsInt(2));
    }
}
