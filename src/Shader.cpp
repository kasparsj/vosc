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
    random();
}

Shader::~Shader(){
}

void Shader::update(const glm::vec2 &pos, const glm::vec2 &size) {
    if (!shaders[name].isLoaded()) {
        shaders[name].load("", "shaders/" + name + ".frag");
    }
    if (!fbo.isAllocated() || (fbo.getWidth() != size.x || fbo.getHeight() != size.y)) {
        fbo.clear();
        fbo.allocate(size.x, size.y);
    }
	fbo.begin();
	shaders[name].begin();
	shaders[name].setUniform1f("time", ofGetElapsedTimef());
	shaders[name].setUniform2f("resolution", ofGetWidth(), ofGetHeight());
    shaders[name].setUniform2f("offset", pos.x, pos.y);
	ofDrawRectangle(0, 0, size.x, size.y);
	shaders[name].end();
	fbo.end();
}

void Shader::draw(int left, int top, int width, int height) {
    fbo.draw(left, top, width, height);
}

void Shader::random() {
    auto it = shaders.begin();
    advance(it, int(ofRandom(shaders.size())));
    name = it->first;
}

void Shader::reload() {
    ofShader sh;
    shaders[name] = sh;
}
