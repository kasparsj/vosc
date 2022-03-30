#pragma once
#include "ofMain.h"

class Shader {
public:
    static map<string, ofShader> shaders;
    
	Shader();
    ~Shader();
	void update(const glm::vec2 &pos, const glm::vec2 &size);
	void draw(int left, int top, int width, int height);
    void random();
    void reload();

	ofFbo fbo;
	string name;
};

