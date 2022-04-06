#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "LayerData.h"
#include "Gen.h"

class Layer {
public:
    static Gen* factory(string type, string name);
    static Gen* factory(string source);
    
    ~Layer() {
        if (data != NULL) {
            delete data;
            data = NULL;
        }
    }
    void setup(int index, int numVisuals, string dataSource = "");
    void layout(Layout layout);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &config);
    void draw(float left, float top, float width, float height);
    void draw();
    void setDataSources(vector<string> ds);
    void addDataSources(vector<string> ds);
    void load(string source);
    void choose(string type = "");
    void unload();
    void setBri(float bri);
    void setAlpha(float alpha);
    void seek(float pos);
    void reload();
    void setNoClear(bool noClear);
    void clear();
    void reset();
    
    int index;
    int total;
    LayerData *data;
    Gen *gen;
    glm::vec2 pos;
    glm::vec2 size;
    ofFloatColor color = ofFloatColor(0, 0);
    float speed = 1.f;
    bool useMFCC = false;
    bool isTidal = false;
    Config config = Config(0);
private:
    vector<string> dataSources;
};
