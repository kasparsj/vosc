#include "Shader.h"
#include "Layer.h"

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
    vector<string> fragPaths = {
        ofToDataPath(path),
        ofToDataPath(path + ".frag"),
    };
    if (!ofFilePath::isAbsolute(path)) {
        fragPaths.insert(fragPaths.end(), {
            ofToDataPath("shaders/" + path),
            ofToDataPath("shaders/" + path + ".frag"),
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
        if (cache.find(path) == cache.end()) {
            ofxAutoReloadedShader sh;
            cache[path] = sh;
        }
        shader = &cache[pathNoExt];
        return cache[pathNoExt].load(vertPath, fragPath, geomPath);
    }
    ofLog() << "could not find shader " << path;
    return false;
}

void Shader::update(Layer* layer) {
    if (isLoaded()) {
        for (map<string, Texture>::iterator it=textures.begin(); it!=textures.end(); ++it) {
            it->second.update(layer);
        }
    }
    else if (hasDefaultTexture()) {
        getDefaultTexture().update(layer);
    }
}

void Shader::begin(Layer *layer) {
    if (isLoaded()) {
        shader->begin();
        shader->setUniform1f("time", layer->data.time);
        shader->setUniform2f("resolution", layer->size.x, layer->size.y);
        shader->setUniform2f("offset", layer->pos.x, layer->pos.y);
        shader->setUniform1i("index", layer->index);
        shader->setUniform4f("color", layer->getColor());
        shader->setUniform1i("random", layer->randomSeed);
        shader->setUniform1i("num_values", layer->data.values.size());
        shader->setUniform1fv("values", layer->data.values.data(), layer->data.values.size());
        shader->setUniform1i("onset", layer->data.onset ? 1 : 0);
        int texLoc = 0;
        for (map<string, Texture>::iterator it=textures.begin(); it!=textures.end(); ++it) {
            if (it->second.hasTexture(layer->delay)) {
                shader->setUniformTexture(it->first, it->second.getTexture(layer->delay), texLoc++);
            }
        }
        for (map<string, vector<float>>::iterator it=uniforms.begin(); it!=uniforms.end(); ++it) {
            if (it->second.size() == 1) {
                shader->setUniform1f(it->first, it->second[0]);
            }
            else if (it->second.size() == 2) {
                shader->setUniform2f(it->first, it->second[0], it->second[1]);
            }
            else if (it->second.size() == 3) {
                shader->setUniform3f(it->first, it->second[0], it->second[1], it->second[2]);
            }
        }
    }
}

void Shader::end() {
    if (isLoaded()) {
        shader->end();
    }
}

void Shader::reset() {
    textures.empty();
    uniforms.empty();
    
}

void Shader::setUniform(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    vector<float> value;
    for (int i=2; i<m.getNumArgs(); i++) {
        value.push_back(m.getArgAsFloat(i));
    }
    setUniform(name, value);
}

void Shader::setUniform(string name, const vector<float>& value) {
    uniforms.erase(name);
    if (value.size()) {
        uniforms[name] = value;
    }
}

Texture& Shader::getDefaultTexture(bool create) {
    if (create && textures.find(DEFAULT_TEX) == textures.end()) {
        Texture tex;
        textures[DEFAULT_TEX] = tex;
    }
    return getTexture(DEFAULT_TEX);
}

void Shader::setTexture(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    setTexture(name, m, 2);
}

void Shader::setTexture(string name, const ofxOscMessage& m, int arg) {
    textures.erase(name);
    Texture tex;
    tex.load(m, arg);
    textures[name] = tex;
}
