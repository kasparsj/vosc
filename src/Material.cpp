#include "Material.hpp"

Material::Material() {
    setVar("diffuse", settings.diffuse);
    setVar("ambient", settings.ambient);
    setVar("specular", settings.specular);
    setVar("emissive", settings.emissive);
    setVar("shininess", settings.shininess);
}

void Material::update() {
    settings.diffuse = getVarColor("diffuse");
    settings.ambient = getVarColor("ambient");
    settings.specular = getVarColor("specular");
    settings.emissive = getVarColor("emissive");
    settings.shininess = getVar("shininess");
    material.setup(settings);
}

void Material::oscCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/mat/diffuse") {
        setVar("diffuse", m);
    }
    else if (command == "/mat/ambient") {
        setVar("ambient", m);
    }
    else if (command == "/mat/specular") {
        setVar("specular", m);
    }
    else if (command == "/mat/emissive") {
        setVar("emissive", m);
    }
    else if (command == "/mat/shininess") {
        setVar("shininess", m);
    }
}
