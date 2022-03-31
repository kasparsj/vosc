#pragma once

#include "Source.h"
#include "ofMain.h"
#include "Config.h"

class Shader : public Source {
public:
    static map<string, ofShader> shaders;
    
	Shader();
    ~Shader();
	void update(const int index, const glm::vec2 &pos, const glm::vec2 &size, Config &mergedConfig, Config &config);
	void draw(int left, int top, int width, int height) override;
    void choose();
    void reload();

	ofFbo fbo;
    string prevName = "";
    int random = 0;
};

