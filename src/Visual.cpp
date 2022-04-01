#include "Visual.h"

void Visual::setup(int index, int numVisuals, string dataSource)
{
    this->index = index;
    this->total = numVisuals;
    dataSources.clear();
    if (dataSource != "") {
        dataSources.push_back(dataSource);
    }
    data = new VisualData(index, pos, size, color, useMFCC, config);
}

void Visual::layout(Layout layout)
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

void Visual::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig) { 
    data->update(dataSources, sounds, notes, globalConfig);
    if (video.isEnabled()) {
        if (data->onset) {
            video.resetPos();
        }
        video.update(data, config);
    }
    if (shader.isEnabled()) {
        shader.update(data, config);
    }
    if (sketch.isEnabled()) {
        sketch.update(data, config);
    }
}

void Visual::draw() {
    if (video.isEnabled()) {
        float value = (data->values.size() ? data->values[0] : 1.0);
        ofSetColor(value * 255 * video.bri, video.alpha * 255);
        video.draw(pos.x, pos.y, size.x, size.y);
    }
    if (shader.isEnabled()) {
        ofSetColor(255 * shader.bri, shader.alpha * 255);
        shader.draw(pos.x, pos.y, size.x, size.y);
    }
    if (sketch.isEnabled()) {
        ofSetColor(255 * sketch.bri, sketch.alpha * 255);
        sketch.draw(pos.x, pos.y, size.x, size.y);
    }
    data->afterDraw();
}

void Visual::setDataSources(vector<string> ds) {
    dataSources.clear();
    addDataSources(ds);
}

void Visual::addDataSources(vector<string> ds) {
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

void Visual::unload() {
    video.name = "";
    shader.name = "";
    sketch.name = "";
}
