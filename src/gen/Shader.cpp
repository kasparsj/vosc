#include "Shader.h"
#include "Layer.h"

map<string, ofShader> loadLocalShaders()
{
    ofDirectory dir("shaders");
    map<string, ofShader> shaders;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        ofShader sh;
        shaders[file.getFileName()] = sh;
    }
    return shaders;
}

map<string, ofShader> Shader::cache = loadLocalShaders();

string Shader::random() {
    auto it = cache.begin();
    advance(it, int(ofRandom(cache.size())));
    return it->first;
}

void Shader::update(Layer *layer) {
    if (!fbo.isAllocated() || (fbo.getWidth() != layer->size.x || fbo.getHeight() != layer->size.y)) {
        fbo.clear();
        fbo.allocate(layer->size.x, layer->size.y);
    }
    if (cache.find(path) == cache.end()) {
        ofShader sh;
        cache[path] = sh;
    }
    if (!cache[path].isLoaded()) {
        string vertPath = "";
        string fragPath = path;
        if (!ofFilePath::isAbsolute(fragPath)) {
            fragPath = ofToDataPath("shaders/" + path);
            if (!ofFile(fragPath).exists()) {
                fragPath = ofToDataPath(fragPath);
            }
        }
        string tmpVertPath = fragPath.substr(0, fragPath.find("." + ofFilePath::getFileExt(fragPath))) + ".vert";
        if (ofFile(tmpVertPath).exists()) {
            vertPath = tmpVertPath;
        }
        if (cache[path].load(vertPath, fragPath)) {
            prevPath = path;
            layer->randomSeed = ofRandom(1000);
        }
        else {
            ofLog() << "could not load shader: " << path;
            path = prevPath;
            return;
        }
    }
    ofEnableAlphaBlending();
	fbo.begin();
    if (!layer->noClear) {
        ofClear(0, 0, 0, 0);
    }
    if (layer->data->visible) {
        cache[path].begin();
        cache[path].setUniform1f("time", layer->data->time);
        cache[path].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
        cache[path].setUniform2f("offset", layer->pos.x, layer->pos.y);
        cache[path].setUniform1i("index", layer->index);
        cache[path].setUniform4f("color", layer->getColor());
        cache[path].setUniform1i("random", layer->randomSeed);
        cache[path].setUniform1i("num_values", layer->data->values.size());
        cache[path].setUniform1fv("values", layer->data->values.data(), layer->data->values.size());
        cache[path].setUniform1i("visible", layer->data->visible ? 1 : 0);
        cache[path].setUniform1i("onset", layer->data->onset ? 1 : 0);
        ofDrawRectangle(0, 0, layer->size.x, layer->size.y);
        cache[path].end();
    }
    fbo.end();
    ofDisableAlphaBlending();
    if (layer->randomShader()) {
        choose();
    }
}

void Shader::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    fbo.draw(pos - size/2.f, size.x, size.y);
}

void Shader::choose() {
    path = random();
}

void Shader::reload() {
    cache.erase(path);
}
