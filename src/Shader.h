#pragma once
#include "Source.h"
#include "ofMain.h"

class Shader : public Source {
public:
    static map<string, ofShader> shaders;
    
	Shader();
    ~Shader();
	void update(const glm::vec2 &pos, const glm::vec2 &size);
	void draw(int left, int top, int width, int height) override;
    void random();
    void reload();

	ofFbo fbo;
};

