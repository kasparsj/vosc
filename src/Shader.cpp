#include "Shader.h"
#include "Layer.h"

map<string, ofxAutoReloadedShader> loadLocalShaders()
{
    ofDirectory dir("shaders");
    map<string, ofxAutoReloadedShader> shaders;
    for (int i = 0; i < dir.getFiles().size(); i++) {
        ofFile file = dir.getFile(i);
        ofxAutoReloadedShader sh;
        shaders[file.getFileName()] = sh;
    }
    return shaders;
}

map<string, ofxAutoReloadedShader> Shader::cache = loadLocalShaders();

string Shader::random() {
    auto it = cache.begin();
    advance(it, int(ofRandom(cache.size())));
    return it->first;
}

bool Shader::load(string path) {
    if (cache.find(path) == cache.end()) {
        ofxAutoReloadedShader sh;
        cache[path] = sh;
    }
    string vertPath = "shaders/passthru.vert";
    string fragPath = path;
    string geomPath = path;
    if (!ofFilePath::isAbsolute(fragPath)) {
        fragPath = ofToDataPath("shaders/" + path);
        if (!ofFile(fragPath).exists()) {
            fragPath = ofToDataPath(fragPath);
        }
    }
    if (!ofFilePath::isAbsolute(geomPath)) {
        geomPath = ofToDataPath("shaders/" + path);
        if (!ofFile(fragPath).exists()) {
            geomPath = ofToDataPath(geomPath);
        }
    }
    string tmpVertPath = fragPath.substr(0, fragPath.find("." + ofFilePath::getFileExt(fragPath))) + ".vert";
    if (ofFile(tmpVertPath).exists()) {
        vertPath = tmpVertPath;
    }
    shader = &cache[path];
    return cache[path].load(vertPath, fragPath, geomPath);
}

void Shader::begin(Layer *layer) {
    if (isLoaded()) {
        shader->begin();
        shader->setUniform1f("time", layer->data.time);
        shader->setUniform2f("resolution", ofGetWidth(), ofGetHeight());
        shader->setUniform2f("offset", layer->pos.x, layer->pos.y);
        shader->setUniform1i("index", layer->index);
        shader->setUniform4f("color", layer->getColor());
        shader->setUniform1i("random", layer->randomSeed);
        shader->setUniform1i("num_values", layer->data.values.size());
        shader->setUniform1fv("values", layer->data.values.data(), layer->data.values.size());
        shader->setUniform1i("onset", layer->data.onset ? 1 : 0);
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

void Shader::setUniform(string name, const ofxOscMessage& m) {
    uniforms.erase(name);
    if (m.getNumArgs() > 2) {
        vector<float> value;
        for (int i=2; i<m.getNumArgs(); i++) {
            value.push_back(m.getArgAsFloat(i));
        }
        uniforms[name] = value;
    }
}
