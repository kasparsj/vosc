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

void Shader::update(const int index, const glm::vec2 &pos, const glm::vec2 &size, Config &mergedConfig, Config &config) {
    Source::update(mergedConfig);
    if (name != prevName) {
        prevName = name;
        random = ofRandom(1000);
        config.color = ofFloatColor(0.4, 1.0/1.5, 1.0);
        mergedConfig.color = config.color;
    }
    if (!shaders[name].isLoaded()) {
        shaders[name].load("", "shaders/" + name + ".frag");
    }
    if (!fbo.isAllocated() || (fbo.getWidth() != size.x || fbo.getHeight() != size.y)) {
        fbo.clear();
        fbo.allocate(size.x, size.y);
    }
	fbo.begin();
    ofClear(0, 0, 0, 0);
	shaders[name].begin();
	shaders[name].setUniform1f("time", time);
	shaders[name].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
    shaders[name].setUniform2f("offset", pos.x, pos.y);
    shaders[name].setUniform1i("index", index);
    shaders[name].setUniform4f("color", mergedConfig.color);
    shaders[name].setUniform1i("random", random);
	ofDrawRectangle(0, 0, size.x, size.y);
	shaders[name].end();
	fbo.end();
    if (mergedConfig.randomShader()) {
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
