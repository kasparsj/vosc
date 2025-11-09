#include "Texture.h"
#include "SketchTex.h"
#include "Config.h"
#include "Layer.h"
#include "VariablePool.h"
#include "Args.h"

void Texture::load(string source, const vector<float>& args) {
    _unload();
    bool explicitType = source.find(":") != string::npos;
    bool _isURL = Args::isURL(source);
    if (explicitType && !_isURL) {
        tex = Tex::factory(source, args);
    }
    else {
        string extension = ofFile(source).getExtension();
        for (int i=0; i<extension.size(); i++) {
            extension[i] = tolower(extension[i]);
        }
        if (extension == "frag") {
            tex = Tex::factory("shader", source, args);
        }
        else if (extension == "jpg" || extension == "jpeg" || extension == "png") {
            tex = Tex::factory("image", source, args);
        }
        else if (extension == "mov" || extension == "mp4") {
            tex = Tex::factory("video", source, args);
        }
        else if (extension == "hpv") {
            tex = Tex::factory("hpv", source, args);
        }
        else if (extension == "html" || _isURL) {
            tex = Tex::factory("html", source, args);
        }
        else {
            if (Args::isHexColor(source)) {
                ofFloatColor color = Args::parseHexColor(source);
                vector<float> args1 = {color.r, color.g, color.b, color.a};
                tex = Tex::factory("color", "color", args1);
            }
            else if (SketchTex::exists(source)) {
                tex = Tex::factory("sketch", source, args);
            }
        }
    }
    if (tex == NULL && source != "") {
        ofLogError() << ("invalid source: " + source);
    }
}

void Texture::load(const ofxOscMessage &m, int arg) {
    string newPath = m.getArgAsString(arg);
    vector<float> args;
    for (int i=(arg+1); i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    load(newPath, args);
}

void Texture::loadData(const ofxOscMessage &m, int arg) {
    if (tex == NULL) {
        vector<float> args;
        tex = make_shared<GenericTex>("loadData", args);
    }
    shared_ptr<Variable<ofFloatColor>> var = make_shared<Variable<ofFloatColor>>();
    var->set(m, arg);
    var->update();
    tex->update(data);
    tex->getTexture().loadData(var->asBufferObject(), GL_RGBA, GL_FLOAT);
    var = NULL;
}

void Texture::choose(const ofxOscMessage& m) {
    _unload();
    string type = m.getNumArgs() > 1 ? m.getArgAsString(1) : "";
    vector<float> args;
    for (int i=2; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    tex = chooseTex(type, args);
    if (tex == NULL) {
        ofLogError() << ("chooseTex: invalid source type " + type);
    }
}

shared_ptr<Tex> Texture::chooseTex(string type, const vector<float>& args) {
    if (type == "") {
        auto it = SourceMap.begin();
        advance(it, int(ofRandom(SourceMap.size())));
        type = it->first;
    }
    return Tex::factory(type, args);
}

void Texture::_unload() {
    frames.clear();
    frames.resize(numFrames);
    tex = NULL;
}

void Texture::unload() {
    _unload();
    vars.clear();
    VariablePool::cleanup(_id);
}

void Texture::reload() {
    if (tex != NULL) {
        tex->reload();
    }
    else {
        ofLog() << "cannot reload layer";
    }
}

void Texture::clear() {
    if (tex != NULL) {
        tex->clear();
    }
    else {
        ofLog() << "cannot clear layer";
    }
}

void Texture::update(const vector<TidalNote> &notes) {
    if (isLoaded()) {
        data.update(notes);
        tex->update(data);
        
        // if var->size() > 1
        // make a loop to draw the tex into a parent
        
        if (looper != NULL) {
            looper->swapBuffers(/*forceSwap*/);
            if (tex->isFrameNew()){
                looper->addFrame(tex->getPixels());
            }
            looper->update();
        }
        
        if (numFrames > 1) {
            drawFrame();
        }
    }
}

void Texture::oscCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/tex") {
        load(m);
    }
    else if (command == "/tex/data") {
        loadData(m);
    }
    else if (command == "/tex/choose") {
        choose(m);
    }
    else if (command == "/tex/reload") {
        reload();
    }
    else if (command == "/tex/unload") {
        unload();
    }
    else if (command == "/tex/clear") {
        clear();
    }
    else if (command == "/tex/seek") {
        setVar("timePct", m, 1);
    }
    else if (command == "/tex/set") {
        string method = m.getArgAsString(1);
        if (method == "textureWrap") {
            getTexture().setTextureWrap(m.getArgAsInt(2), m.getArgAsInt(3));
        }
        else if (method == "numFrames") {
            setNumFrames(m.getArgAsInt(2));
        }
        else {
            data.oscCommand(command, m);
        }
    }
    else if (command == "/tex/looper") {
        setLooper(m);
    }
    else if (command == "/tex/color") {
        setVar("color", m);
    }
    else if (command == "/tex/tint") {
//        // todo: there is no way to multiply tints, therefore texture tint is disabled temporarily
//        setVar("tint", m);
        ofLogError() << "/tex/tint is disabled. Use /layer/tint.";
    }
    else if (command == "/tex/speed") {
        setVar("speed", m);
    }
    else if (command == "/tex/var" || command == "/tex/uniform") {
        string name = m.getArgAsString(1);
        setVar(name, m, 2);
    }
    else if (command == "/tex/var/lifo") {
        string name = m.getArgAsString(1);
        float value = m.getArgAsFloat(2);
        // todo: should check type is float
        if (hasVar(name)) {
            int maxLen = m.getArgAsInt(3);
            vector<float>& values = getVarVec(name);
            values.push_back(value);
            if (values.size() > maxLen) {
                values.erase(values.begin());
            }
        }
        else {
            setVar(name, value);
        }
    }
    else {
        data.oscCommand(command, m);
    }
}

void Texture::drawFrame() {
    curFbo = (curFbo + 1) % frames.size();
    ofFbo& fbo = frames[curFbo];
    if (!fbo.isAllocated()) {
        // todo: should frame fboSettings be different or same?
        data.allocate(fbo);
    }
    fbo.begin();
    ofClear(0, 0, 0, 0);
    texDraw(glm::vec2(0, 0), data.getSize());
    fbo.end();
}

void Texture::draw(Layer* layer) {
    const glm::vec3& pos = layer->getVarVec3("pos");
    const glm::vec2& size = layer->data.getSize();
    if (isLoaded() && layer->delay == 0) {
        texDraw(pos, size);
        data.afterDraw(vars);
    }
    else if (hasTexture(layer->delay)) {
        // todo: there is no way to multiply tints, therefore texture tint is disabled temporarily
        //ofPushStyle();
        //ofSetColor(getVarColor("tint"));
        const ofTexture& tex = getTexture(layer->delay);
        if (data.aspectRatio) {
            if (tex.getWidth() > tex.getHeight()) {
                tex.draw(pos, size.x, size.x/tex.getWidth() * tex.getHeight());
            }
            else {
                tex.draw(pos, size.y/tex.getHeight() * tex.getWidth(), size.y);
            }
        }
        else {
            tex.draw(pos, size.x, size.y);
        }
        //ofPopStyle();
    }
}

void Texture::texDraw(const glm::vec2& pos, const glm::vec2 size) {
    // todo: there is no way to multiply tints, therefore texture tint is disabled temporarily
    //ofPushStyle();
    //ofSetColor(getVarColor("tint"));
    if (looper == NULL) {
        tex->draw(pos, size);
    }
    else {
        looper->draw(pos.x, pos.y, size.x, size.y);
    }
    //ofPopStyle();
}

void Texture::setNumFrames(int value) {
    numFrames = value;
    frames.resize(numFrames);
}

void Texture::reset() {
    if (tex != NULL) {
        tex->reset();
    }
    vars.clear();
    VariablePool::cleanup(_id);
    setVar("size", glm::vec3());
    setVar("speed", 1.f);
    setVar("color", ofFloatColor(1.f, 1.f));
    setVar("tint", ofFloatColor(1.f, 1.f));
    setVar("timePct", 0.f);
}

const ofFbo& Texture::getFrame(int delay) const {
    int i = curFbo - delay;
    while (i<0) i += frames.size();
    return frames[MIN(frames.size()-1, i)];
}

ofFbo& Texture::getFrame(int delay) {
    int i = curFbo - delay;
    while (i<0) i += frames.size();
    return frames[MIN(frames.size()-1, i)];
}

bool Texture::hasTexture(int delay) const {
    if (numFrames <= 1) {
        return isLoaded() && tex->getTexture().isAllocated();
    }
    if (curFbo < 0) return false;
    const ofFbo& fbo = getFrame(delay);
    return fbo.isAllocated() && fbo.getTexture().isAllocated();
}

const ofTexture& Texture::getTexture(int delay, int att) const {
    if (numFrames <= 1) {
        if (looper == NULL) {
            return tex->getTexture(att);
        }
        else {
            return looper->getFbo().getTexture(att);
        }
    }
    return getFrame(delay).getTexture(att);
}

int Texture::getNumTextures(int delay) const {
    if (numFrames <= 1) {
        if (looper == NULL) {
            return tex->getNumTextures();
        }
        else {
            return looper->getFbo().getNumTextures();
        }
    }
    return getFrame(delay).getNumTextures();
}

ofTexture& Texture::getTexture(int delay, int att) {
    if (numFrames <= 1) {
        if (looper == NULL) {
            return tex->getTexture(att);
        }
        else {
            return looper->getFbo().getTexture(att);
        }
    }
    return getFrame(delay).getTexture(att);
}

ofPixels& Texture::getPixels() const {
    return tex->getPixels();
}

void Texture::setLooper(const ofxOscMessage& m) {
    if (m.getArgAsFloat(1) == 0) {
        delete looper;
        looper = NULL;
    }
    else {
        float maxDuration = m.getArgAsFloat(1);
        int fps = m.getNumArgs() > 2 ? m.getArgAsInt(2) : 30;
        int speed = m.getNumArgs() > 3 ? m.getArgAsInt(3) : 2.0;
        if (looper == NULL) {
            looper = new ofxLooper();
            looper->setup(maxDuration, fps, speed);
        }
        else {
            looper->setMaxDuration(maxDuration);
            looper->setFps(fps);
            looper->setPlaySpeed(speed);
        }
    }

}
