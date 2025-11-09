#pragma once

#include "BaseTex.h"

class Tex : public BaseTex {
public:
    virtual void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void clear() override;
    virtual ofTexture & getTexture() override {
        return tex;
    }
    virtual const ofTexture & getTexture() const override {
        return tex;
    }
    
private:
    ofTexture tex;
};

