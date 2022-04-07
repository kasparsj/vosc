#include "Layer.h"
#include "Shader.h"
#include "Video.h"
#include "Sketch.h"

Gen* Layer::factory(string type, string name) {
    Gen *gen = NULL;
    auto it = SourceMap.find(type);
    if (it != SourceMap.end()) {
        switch (it->second) {
            case S_VIDEO:
                gen = new Video(name);
                break;
            case S_SHADER:
                gen = new Shader(name);
                break;
            case S_SKETCH:
                gen = new Sketch(name);
                break;
        }
    }
    return gen;
}

Gen* Layer::factory(string source) {
    string type = source;
    string name = "";
    if (source.find(":") != string::npos) {
        type = source.substr(0, source.find(":"));
        name = source.substr(source.find(":") + 1);
    }
    if (name == "") {
        auto it = SourceMap.find(type);
        if (it != SourceMap.end()) {
            switch (it->second) {
                case S_VIDEO:
                    name = Video::random();
                    break;
                case S_SHADER:
                    name = Shader::random();
                    break;
                case S_SKETCH:
                    name = Sketch::random();
                    break;
            }
        }
    }
    return factory(type, name);
}

void Layer::setup(int index, int numVisuals, string dataSource)
{
    this->index = index;
    this->total = numVisuals;
    dataSources.clear();
    if (dataSource != "") {
        dataSources.push_back(dataSource);
    }
}

void Layer::layout(Layout layout)
{
    switch (layout) {
        case L_COLUMN:
            pos = glm::vec2(0, ofGetHeight() / total * index);
            size = glm::vec2(ofGetWidth(), ofGetHeight() / total);
            break;
        case L_ROW:
            pos = glm::vec2(ofGetWidth() / total * index, 0);
            size = glm::vec2(ofGetWidth() / total, ofGetHeight());
            break;
        case L_GRID: {
            int half = (int) ceil(total / 2.f);
            pos = glm::vec2(ofGetWidth() / half * (index % half), ofGetHeight() / 2 * floor(index / half));
            size = glm::vec2(ofGetWidth() / half, ofGetHeight() / 2);
            break;
        }
        case L_STACK:
            pos = glm::vec2(0, 0);
            size = glm::vec2(ofGetWidth(), ofGetHeight());
            break;
    }
}

void Layer::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    if (data != NULL) {
        data->update(sounds, notes);
    }
    if (gen != NULL) {
        gen->update(this);
    }
}

void Layer::draw(float left, float top, float width, float height) {
    if (gen != NULL) {
        ofSetColor(255 * bri, alpha * 255);
        gen->draw(left, top, width, height);
    }
}

void Layer::draw() {
    draw(pos.x, pos.y, size.x, size.y);
    if (data != NULL) {
        data->afterDraw();
    }
}

void Layer::setDataSources(vector<string> ds) {
    dataSources.clear();
    addDataSources(ds);
}

void Layer::addDataSources(vector<string> ds) {
    for (int i=0; i<ds.size(); i++) {
        string dsName = ds[i];
        string dsMax = "";
        if (dsName.find(":") != string::npos) {
            dsMax = dsName.substr(dsName.find(":") + 1);
            dsName = dsName.substr(0, dsName.find(":"));
        }
        auto it = DataSourceMap.find(dsName);
        if (it != DataSourceMap.end()) {
            // todo: should hold struct instead of string
            dataSources.push_back(it->first + dsMax);
        }
        else {
            ofLog() << "invalid data source " << ds[i];
        }
    }
}

void Layer::load(string source) {
    if (source.find(":") != string::npos) {
        gen = factory(source);
        if (gen != NULL) {
            data = new LayerData(this);
        }
        else {
            ofLog() << "invalid source type " << source;
        }
    }
    else {
        if (source == "") {
            unload();
        }
        else {
            ofLog() << "invalid source " << source;
        }
    }
}

void Layer::choose(string type) {
    if (type == "") {
        auto it = SourceMap.begin();
        advance(it, int(ofRandom(SourceMap.size())));
        type = it->first;
    }
    gen = factory(type);
    if (gen != NULL) {
        data = new LayerData(this);
    }
    else {
        ofLog() << "invalid source type " << type;
    }
}

void Layer::unload() {
    if (gen != NULL) {
        delete gen;
        gen = NULL;
    }
    if (data != NULL) {
        delete data;
        data = NULL;
    }
}

void Layer::reload() {
    if (gen != NULL) {
        gen->reload();
    }
    else {
        ofLog() << "cannot reload layer " << index;
    }
}

void Layer::clear() {
    if (gen != NULL) {
        gen->clear();
    }
    else {
        ofLog() << "cannot clear layer " << index;
    }
}

void Layer::reset() {
    if (gen != NULL) {
        gen->reset();
    }
    else {
        ofLog() << "cannot reset layer " << index;
    }
}
