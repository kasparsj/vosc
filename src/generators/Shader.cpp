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

Shader::Shader(){
}

Shader::~Shader(){
}

void Shader::update(VisualData *data, Config &config) {
    Generator::update(data->mergedConfig);
    if (name != prevName) {
        prevName = name;
        random = ofRandom(1000);
        config.color = ofFloatColor(0.4, 1.0/1.5, 1.0);
        data->mergedConfig.color = config.color;
    }
    if (!shaders[name].isLoaded()) {
        shaders[name].load("", "shaders/" + name + ".frag");
    }
    if (!fbo.isAllocated() || (fbo.getWidth() != data->size.x || fbo.getHeight() != data->size.y)) {
        fbo.clear();
        fbo.allocate(data->size.x, data->size.y);
    }
    ofEnableAlphaBlending();
	fbo.begin();
    if (!noClear) {
        ofClear(0, 0, 0, 0);
    }
	shaders[name].begin();
	shaders[name].setUniform1f("time", time);
	shaders[name].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
    shaders[name].setUniform2f("offset", data->pos.x, data->pos.y);
    shaders[name].setUniform1i("index", data->index);
    shaders[name].setUniform4f("color", data->mergedConfig.color);
    shaders[name].setUniform1i("random", random);
    shaders[name].setUniform1i("num_values", data->values.size());
    shaders[name].setUniform1fv("values", data->values.data(), data->values.size());
    shaders[name].setUniform1i("visible", data->visible ? 1 : 0);
    shaders[name].setUniform1i("onset", data->onset ? 1 : 0);
	ofDrawRectangle(0, 0, data->size.x, data->size.y);
	shaders[name].end();
	fbo.end();
    ofDisableAlphaBlending();
    if (data->mergedConfig.randomShader()) {
        choose();
    }
}

void Shader::draw(int left, int top, int width, int height) {
    fbo.draw(left, top, width, height);
}

void Shader::choose() {
    auto it = shaders.begin();
    advance(it, int(ofRandom(shaders.size())));
    name = it->first;
}

void Shader::reload() {
    ofShader sh;
    shaders[name] = sh;
}