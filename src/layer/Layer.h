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

class Camera;

class Layer : public VarsHolder {
public:
    Layer() : VarsHolder(), data(*this) {
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
    void draw(const glm::vec3 &pos, const glm::vec2 &size, const Camera* camera = nullptr);
    void draw(int totalVisible, const Camera* camera = nullptr);
    void doAlign();
    void doRotate(const glm::vec3& pos);
    void doScale();
    void unload();
    void reset();
    void resetTransform();    
    void setShader(string path);
    bool hasGeom() {
        return geom != nullptr && geom->isLoaded();
    }
    void setGeom(const shared_ptr<Geom>& value) {
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
