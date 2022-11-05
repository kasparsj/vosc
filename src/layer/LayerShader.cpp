#include "LayerShader.h"

void LayerShader::load(string path) {
    shader.load(path);
}

void LayerShader::begin() {
    if (shader.isLoaded()) {
        shader.begin();
    }
}

void LayerShader::end() {
    if (shader.isLoaded()) {
        shader.end();
    }
}
