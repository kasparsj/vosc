#include "Buffer.hpp"
#include "VariablePool.h"

Buffer::Buffer(string name, const ofxOscMessage& m, int arg, VarsHolder* parent) {
    shared_ptr<BaseVar> vartmp;
    if (m.getArgType(arg) == OFXOSC_TYPE_STRING) {
        string source = m.getArgAsString(arg);
        if (VariablePool::hasShared(source)) {
            vartmp = VariablePool::getShared(source);
        }
    }
    if (vartmp == NULL) {
        vartmp = VariablePool::createOrUpdate(name, m, arg, parent);
    }
    var = vartmp;
    buf.allocate();
    buf.bind(GL_TEXTURE_BUFFER);
    BufData data = var->asBufferData();
    buf.setData(data.bytes, data.data, GL_STREAM_DRAW);
    tex.allocateAsBufferTexture(buf, GL_RGBA32F);
}

void Buffer::update() {
    BufData data = var->asBufferData();
    buf.updateData(data.bytes, data.data);
}
