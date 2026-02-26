#include "Buffer.hpp"
#include "VariablePool.h"

Buffer::Buffer(string name, const ofxOscMessage& m, int arg, VarsHolder& parent) {
    shared_ptr<BaseVar> vartmp;
    if (m.getArgType(arg) == OFXOSC_TYPE_STRING) {
        string source = m.getArgAsString(arg);
        if (VariablePool::hasShared(source)) {
            vartmp = VariablePool::getShared(source);
        }
    }
    if (vartmp == nullptr) {
        vartmp = VariablePool::createOrUpdate(name, m, arg, &parent);
    }
    var = vartmp;
    buf.allocate();
    buf.bind(GL_TEXTURE_BUFFER);
    static const float kZeroRGBA[4] = {0.f, 0.f, 0.f, 0.f};
    BufData data = var->asBufferData();
    if (data.bytes > 0 && data.data != nullptr) {
        buf.setData(data.bytes, data.data, GL_STREAM_DRAW);
    }
    else {
        buf.setData(sizeof(kZeroRGBA), kZeroRGBA, GL_STREAM_DRAW);
    }
    tex.allocateAsBufferTexture(buf, GL_RGBA32F);
}

void Buffer::update() {
    static const float kZeroRGBA[4] = {0.f, 0.f, 0.f, 0.f};
    BufData data = var->asBufferData();
    if (data.bytes > 0 && data.data != nullptr) {
        buf.updateData(data.bytes, data.data);
    }
    else {
        buf.updateData(sizeof(kZeroRGBA), kZeroRGBA);
    }
}
