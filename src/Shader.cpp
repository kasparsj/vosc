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
    }
}

void Shader::end() {
    if (isLoaded()) {
        shader->end();
    }
}
