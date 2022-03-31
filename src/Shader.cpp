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

void Shader::update(ShaderData *shaderData, Config &config) {
    Source::update(shaderData->mergedConfig);
    if (name != prevName) {
        prevName = name;
        random = ofRandom(1000);
        config.color = ofFloatColor(0.4, 1.0/1.5, 1.0);
        shaderData->mergedConfig.color = config.color;
    }
    if (!shaders[name].isLoaded()) {
        shaders[name].load("", "shaders/" + name + ".frag");
    }
    if (!fbo.isAllocated() || (fbo.getWidth() != shaderData->size.x || fbo.getHeight() != shaderData->size.y)) {
        fbo.clear();
        fbo.allocate(shaderData->size.x, shaderData->size.y);
    }
    ofEnableAlphaBlending();
	fbo.begin();
    if (!noClear) {
        ofClear(0, 0, 0, 0);
    }
	shaders[name].begin();
	shaders[name].setUniform1f("time", time);
	shaders[name].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
    shaders[name].setUniform2f("offset", shaderData->pos.x, shaderData->pos.y);
    shaders[name].setUniform1i("index", shaderData->index);
    shaders[name].setUniform4f("color", shaderData->mergedConfig.color);
    shaders[name].setUniform1i("random", random);
    shaders[name].setUniform1fv("values", shaderData->values, MAX_VISUALS);
    shaders[name].setUniform1i("visible", shaderData->visible ? 1 : 0);
    shaders[name].setUniform1i("onset", shaderData->onset ? 1 : 0);
    ofSetColor(255);
	ofDrawRectangle(0, 0, shaderData->size.x, shaderData->size.y);
	shaders[name].end();
	fbo.end();
    ofDisableAlphaBlending();
    if (shaderData->mergedConfig.randomShader()) {
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
