#include "Light.hpp"
#include "Args.h"

Light::Light() {
    setVar("pos", glm::vec3());
}

void Light::set(const ofxOscMessage& m, int idx) {
    setVar("pos", Args::parse<glm::vec3>(m, idx));
}
