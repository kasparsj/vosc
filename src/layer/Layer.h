#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "LayerData.h"
#include "Geom.h"
#include "LayerShader.h"
#include "GeomPool.h"

class Layer : public VarsConfig {
public:
    Layer() {
        static int nextId = 0;
        _id = nextId++;
    }
    ~Layer() {
        reset();
    }
    void setup(int index);
    void layout(Layout layout, int layoutIndex, int layoutTotal);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void drawToFbo();
    void draw(const glm::vec3 &pos, const glm::vec2 &size);
    void draw(int totalVisible);
    void doAlign();
    void doRotate();
    void doScale();
    void reset();
    void resetTransform();    
    void setShader(string path);
    bool hasGeom() {
        return geom != NULL && geom->isLoaded();
    }
    void setGeom(Geom* value) {
        geom = value;
    }
    int getId() {
        return _id;
    }
    
    int index;
    Geom* geom = NULL;
    LayerData data;
    LayerShader shader;
    glm::vec3 pos;
    glm::vec3 rotation = glm::vec3(0, 0, 0);
    glm::vec3 rotationPoint = glm::vec3(0, 0, 0);
    glm::vec3 rotationSpeed = glm::vec3(0, 0, 0);
    glm::vec3 scale = glm::vec3(1);
    ofAlignHorz alignH;
    ofAlignVert alignV;
    int behaviour = -1;
    float bri = 1.0;
    float alpha = 1.0;
    VarConfig visibleThresh = {"const", 1.f, 0};
    VarConfig onsetThresh = {"const", 1.f, 0};
    uint8_t delay = 0;
    ofMaterialSettings matSettings;
    ofMaterial material;
    
private:
    int _id;
};
