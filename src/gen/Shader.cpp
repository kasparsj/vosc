#include "Shader.h"

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

void Shader::update(LayerData *data, Config &config) {
    if (!fbo.isAllocated() || (fbo.getWidth() != data->size.x || fbo.getHeight() != data->size.y)) {
        fbo.clear();
        fbo.allocate(data->size.x, data->size.y);
    }
    if (name != prevName) {
        if (shaders.find(name) == shaders.end()) {
            ofLog() << "shader " << name << " does not exist";
            name = prevName;
            return;
        }
        prevName = name;
        randomSeed = ofRandom(1000);
    }
    if (!shaders[name].isLoaded()) {
        shaders[name].load("", "shaders/" + name + ".frag");
    }
    ofEnableAlphaBlending();
	fbo.begin();
    if (!noClear) {
        ofClear(0, 0, 0, 0);
    }
    if (data->visible) {
        shaders[name].begin();
        shaders[name].setUniform1f("time", data->time);
        shaders[name].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
        shaders[name].setUniform2f("offset", data->pos.x, data->pos.y);
        shaders[name].setUniform1i("index", data->index);
        shaders[name].setUniform4f("color", data->getColor());
        shaders[name].setUniform1i("random", randomSeed);
        shaders[name].setUniform1i("num_values", data->values.size());
        shaders[name].setUniform1fv("values", data->values.data(), data->values.size());
        shaders[name].setUniform1i("visible", data->visible ? 1 : 0);
        shaders[name].setUniform1i("onset", data->onset ? 1 : 0);
        ofDrawRectangle(0, 0, data->size.x, data->size.y);
        shaders[name].end();
    }
    fbo.end();
    ofDisableAlphaBlending();
    if (data->randomShader()) {
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
