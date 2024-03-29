#pragma once

#include "ofMain.h"

class Layer;
class Texture;
class TexData;

class Tex : public ofBaseHasTexture, public ofBaseHasPixels {
public:
    static shared_ptr<Tex> factory(string type, string name, const vector<float>& args);
    static shared_ptr<Tex> factory(string source, const vector<float>& args);

    Tex(string path, const vector<float>& args) : path(path), args(args) {}
    virtual ~Tex() = default;
    virtual void update(TexData& data) = 0;
    virtual void draw(const glm::vec2 &pos, const glm::vec2 &size) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};
    virtual bool isFrameNew() {
        return true;
    }
    void setUseTexture(bool bUseTex) override {
        
    }
    bool isUsingTexture() const override {
        return false;
    }
    virtual int getNumTextures() {
        return isUsingTexture() ? 1 : 0;
    }
    virtual ofTexture& getTexture() override {
        throw "abstract function Tex::getTexture called";
    }
    virtual const ofTexture& getTexture() const override {
        throw "abstract function Tex::getTexture called";
    }
    virtual ofTexture& getTexture(int att) {
        return this->getTexture();
    }
    virtual const ofTexture& getTexture(int att) const {
        return this->getTexture();
    }
    virtual ofPixels& getPixels() override {
        return pixels;
    }
    virtual const ofPixels& getPixels() const override {
        return pixels;
    }
    string path = "";
    string prevPath = "";
    vector<float> args;
    bool aspectRatio;
    ofPixels pixels;
};
