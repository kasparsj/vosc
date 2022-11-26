#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "LayerData.h"
#include "Geom.h"
#include "LayerShader.h"
#include "GeomPool.h"

class Layer : public VarsHolder {
public:
    Layer() : VarsHolder() {
        data.setup(this);
        reset();
    }
    ~Layer() {
        unload();
    }
    void setup(int index);
    void layout(Layout layout, int layoutIndex, int layoutTotal);
    void update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void drawToFbo();
    void draw(const glm::vec3 &pos, const glm::vec2 &size);
    void draw(int totalVisible);
    void doAlign();
    void doRotate();
    void doScale();
    void unload();
    void reset();
    void resetTransform();    
    void setShader(string path);
    bool hasGeom() {
        return geom != NULL && geom->isLoaded();
    }
    void setGeom(Geom* value) {
        geom = value;
    }
    
    int index;
    Geom* geom = NULL;
    LayerData data;
    LayerShader shader;
    ofAlignHorz alignH;
    ofAlignVert alignV;
    int behaviour = -1;
    uint8_t delay = 0;
    ofMaterialSettings matSettings;
    ofMaterial material;
};
