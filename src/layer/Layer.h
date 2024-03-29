#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "OSCInput.h"
#include "Config.h"
#include "TexData.h"
#include "Geom.h"
#include "LayerShader.h"
#include "GeomPool.h"
#include "Material.hpp"

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
    void update(const vector<TidalNote> &notes);
    void oscCommand(const string& command, const ofxOscMessage &m);
    void layerCommand(const string& command, const ofxOscMessage& m);
    void materialCommand(const string& command, const ofxOscMessage& m);
    void drawToFbo();
    void draw(const glm::vec3 &pos, const glm::vec2 &size);
    void draw(int totalVisible);
    void doAlign();
    void doRotate(const glm::vec3& pos);
    void doScale();
    void unload();
    void reset();
    void resetTransform();    
    void setShader(string path);
    bool hasGeom() {
        return geom != NULL && geom->isLoaded();
    }
    void setGeom(shared_ptr<Geom>& value) {
        geom = value;
    }
    
    int index;
    shared_ptr<Geom> geom;
    TexData data;
    LayerShader shader;
    ofAlignHorz alignH;
    ofAlignVert alignV;
    int behaviour = -1;
    uint8_t delay = 0;
    Material material;
};
