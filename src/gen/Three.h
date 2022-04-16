#pragma once

#include "Gen.h"

class Three : public Gen {
public:
    static vector<string> primitives;
    static bool exists(string path);
    static string random();

    Three(string path) : Gen(path) {}
    ~Three() {
        if (primitive != NULL) {
            delete primitive;
        }
    }
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofFloatColor getTint(Layer *layer) override;

private:
    of3dPrimitive *primitive = NULL;
};
