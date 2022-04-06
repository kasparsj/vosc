#include "Shader.h"
#include "Layer.h"

map<string, ofShader> loadShaders()
{
    ofDirectory dir("shaders");
    map<string, ofShader> shaders;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "frag") {
            ofShader sh;
            string fileName = file.getFileName();
            shaders[fileName.substr(0, fileName.find("." + file.getExtension()))] = sh;
        }
    }
    return shaders;
}

map<string, ofShader> Shader::shaders = loadShaders();

string Shader::random() {
    auto it = shaders.begin();
    advance(it, int(ofRandom(shaders.size())));
    return it->first;
}

Shader::~Shader(){
}

void Shader::update(Layer *layer, const Config &config) {
    if (!fbo.isAllocated() || (fbo.getWidth() != layer->size.x || fbo.getHeight() != layer->size.y)) {
        fbo.clear();
        fbo.allocate(layer->size.x, layer->size.y);
    }
    if (name != prevName) {
        if (shaders.find(name) == shaders.end()) {
            ofLog() << "shader " << name << " does not exist";
            name = prevName;
            return;
        }
        prevName = name;
        layer->randomSeed = ofRandom(1000);
    }
    if (!shaders[name].isLoaded()) {
        shaders[name].load("", "shaders/" + name + ".frag");
    }
    ofEnableAlphaBlending();
	fbo.begin();
    if (!layer->noClear) {
        ofClear(0, 0, 0, 0);
    }
    if (layer->data->visible) {
        shaders[name].begin();
        shaders[name].setUniform1f("time", layer->data->time);
        shaders[name].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
        shaders[name].setUniform2f("offset", layer->pos.x, layer->pos.y);
        shaders[name].setUniform1i("index", layer->index);
        shaders[name].setUniform4f("color", layer->getColor());
        shaders[name].setUniform1i("random", layer->randomSeed);
        shaders[name].setUniform1i("num_values", layer->data->values.size());
        shaders[name].setUniform1fv("values", layer->data->values.data(), layer->data->values.size());
        shaders[name].setUniform1i("visible", layer->data->visible ? 1 : 0);
        shaders[name].setUniform1i("onset", layer->data->onset ? 1 : 0);
        ofDrawRectangle(0, 0, layer->size.x, layer->size.y);
        shaders[name].end();
    }
    fbo.end();
    ofDisableAlphaBlending();
    if (layer->randomShader()) {
        choose();
    }
}

void Shader::draw(int left, int top, int width, int height) {
    fbo.draw(left, top, width, height);
}

void Shader::choose() {
    name = random();
}

void Shader::reload() {
    ofShader sh;
    shaders[name] = sh;
}
