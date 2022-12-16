#include "Buffer.hpp"
#include "VariablePool.h"

Buffer::Buffer(string name, const ofxOscMessage& m, int arg, VarsHolder* parent) {
    string source = m.getArgAsString(arg);
    if (VariablePool::hasShared(source)) {
        var = VariablePool::getShared(source);
    }
    else {
        var = VariablePool::getOrCreate(name, m, arg, parent);
    }
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
