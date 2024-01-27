#include "Light.hpp"
#include "Args.h"

Light::Light() {
    light.enable();
    setVar("pos", glm::vec3());
}

void Light::set(const ofxOscMessage& m, int idx) {
    setVar("pos", Args::parse<glm::vec3>(m, idx));
}

void Light::update() {
    light.setPosition(getVarVec3("pos"));
}
