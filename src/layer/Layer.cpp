#include "Layer.h"
#include "VariablePool.h"

void Layer::setup(int index)
{
    this->index = index;
    reset();
}

void Layer::layout(Layout layout, int layoutIndex, int layoutTotal)
{
    switch (layout) {
        case Layout::COLUMN:
            setVar("pos", glm::vec3(0, ofGetHeight() / layoutTotal * layoutIndex, 0));
            data.setSize(ofGetWidth(), ofGetHeight() / layoutTotal);
            break;
        case Layout::ROW:
            setVar("pos", glm::vec3(ofGetWidth() / layoutTotal * layoutIndex, 0, 0));
            data.setSize(ofGetWidth() / layoutTotal, ofGetHeight());
            break;
        case Layout::GRID: {
            int root = (int) sqrt(layoutTotal);
            setVar("pos", glm::vec3(ofGetWidth() / root * (layoutIndex % root), ofGetHeight() / root * floor(layoutIndex / root), 0));
            data.setSize(ofGetWidth() / root, ofGetHeight() / root);
            break;
        }
        case Layout::STACK:
            setVar("pos", glm::vec3(0, 0, 0));
            data.setSize(ofGetWidth(), ofGetHeight());
            break;
    }
}

void Layer::update(const vector<OSCInput> &inputs, const vector<TidalNote> &notes) {
    if (shader.isLoaded() || shader.hasDefaultTexture() || hasGeom()) {
        data.update(inputs, notes);
    }
    shader.update(inputs, notes);
    material.update();
}

void Layer::oscCommand(const string& command, const ofxOscMessage &m) {
    if (command.substr(0, 4) == "/tex") {
        shared_ptr<Texture>& tex = shader.getDefaultTexture();
        bool isShared = false;
        if (command == "/tex" || command == "/tex/choose" || tex == NULL) {
            if (command == "/tex") {
                string name = m.getArgAsString(1);
                if (TexturePool::hasShared(name)) {
                    tex = TexturePool::getShared(name);
                    isShared = true;
                }
                else {
                    tex = TexturePool::getOrCreate(DEFAULT_TEX, &shader);
                }
            }
            else {
                tex = TexturePool::getOrCreate(DEFAULT_TEX, &shader);
            }
            shader.setDefaultTexture(tex);
            glm::vec2 size = tex->data.getSize();
            if (size.x == 0 && size.y == 0) {
                tex->data.setSize(data.getSize());
            }
        }
        if (isShared) {
            return;
        }
        tex->oscCommand(command, m);
    }
    else if (command.substr(0, 4) == "/var") {
        string name = m.getArgAsString(1);
        setVar(name, m, 2);
    }
    else if (command.substr(0, 5) == "/geom") {
        if (command == "/geom" || command == "/geom/choose") {
            if (command == "/geom") {
                string source = m.getArgAsString(1);
                shared_ptr<Geom>& newGeom = GeomPool::getOrCreate(source, getId());
                setGeom(newGeom);
                if (GeomPool::hasShared(source)) {
                    return;
                }
            }
            else {
                shared_ptr<Geom>& newGeom = GeomPool::getOrCreate(getId());
                newGeom->choose(m);
                setGeom(newGeom);
            }
        }
        geom->oscCommand(command, m);
    }
    else if (command.substr(0, 6) == "/layer") {
        layerCommand(command, m);
    }
    else if (command.substr(0, 7) == "/shader") {
        shader.oscCommand(command, m);
    }
    else if (command.substr(0, 4) == "/mat") {
        material.oscCommand(command, m);
    }
    else {
        ofLog() << "invalid indexCommand: " << m;
    }
}

void Layer::layerCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/layer/bri") {
        setVar("bri", m);
    }
    else if (command == "/layer/alpha") {
        setVar("alpha", m);
    }
    else if (command == "/layer/tint") {
        setVar("tint", m);
    }
    else if (command == "/layer/reset") {
        reset();
    }
    else if (command == "/layer/pos") {
        setVar("pos", m);
    }
    else if (command == "/layer/size") {
        setVar("size", m);
    }
    else if (command == "/layer/rot") {
        setVar("rotation", m);
    }
    else if (command == "/layer/pivot") {
        setVar("pivot", m);
    }
    else if (command == "/layer/scale") {
        setVar("scale", m);
    }
    else if (command == "/layer/align") {
        ofAlignHorz alignH;
        ofAlignVert alignV;
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            string argH = m.getArgAsString(1);
            string argV = m.getNumArgs() > 2 ? m.getArgAsString(2) : argH;
            if (argH == "center") {
                alignH = OF_ALIGN_HORZ_CENTER;
            }
            else if (argH == "left") {
                alignH = OF_ALIGN_HORZ_LEFT;
            }
            else if (argH == "right") {
                alignH = OF_ALIGN_HORZ_RIGHT;
            }
            if (argV == "center") {
                alignV = OF_ALIGN_VERT_CENTER;
            }
            else if (argV == "top") {
                alignV = OF_ALIGN_VERT_TOP;
            }
            else if (argV == "bottom") {
                alignV = OF_ALIGN_VERT_BOTTOM;
            }
        }
        else {
            alignH = static_cast<ofAlignHorz>(m.getArgAsInt(1));
            alignV = static_cast<ofAlignVert>(m.getNumArgs() > 2 ? m.getArgAsInt(2) : m.getArgAsInt(1) * 16);
        }
        alignH = alignH;
        alignV = alignV;
    }
    else if (command == "/layer/delay") {
        delay = m.getArgAsFloat(1);
    }
    else if (command == "/layer/behaviour") {
        behaviour = m.getArgAsInt(1);
    }
    else if (command == "/layer/visible") {
        setVar("visible", m);
    }
}

void Layer::draw(const glm::vec3 &pos, const glm::vec2 &size) {
    //ofSetGlobalAmbientColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    
    ofPushMatrix();
    doAlign();
    doRotate();
    ofPushStyle();
    ofSetColor(getVarColor("tint") * getVar("bri"), getVar("alpha") * 255);
    
    if (hasGeom() || shader.isLoaded()) {
        if (geom == NULL) {
            geom = GeomPool::getOrCreate(getId());
        }
        if (!geom->isLoaded()) {
            geom->load("quad");
        }
        if (shader.hasDefaultTexture()) {
            if (!shader.isLoaded()) {
                shader.load("texture");
            }
        }
        
        ofEnableDepthTest();
        ofTranslate(pos + data.getSize()/2.f);
        doScale();
        ofScale(geom->getScale(data.getSize()));
        
        if (shader.isLoaded()) {
            shader.begin(data, delay);
            shader.setUniform1i("layer", index);
            shader.setUniform2f("offset", pos.x, pos.y);
            //shader.setUniformMaterial(material.get());
            //material.begin();
            geom->draw();
            //material.end();
            shader.end();
        }
        else {
            geom->draw();
        }
    }
    else if (shader.hasDefaultTexture()) {
        doScale();
        shader.getDefaultTexture()->draw(this);
    }
    
    ofPopStyle();
    ofPopMatrix();
}

void Layer::draw(int totalVisible) {
    if (shader.isLoaded() || shader.hasDefaultTexture() || hasGeom()) {
        if (getVarBool("visible")) {
            switch (data.blendMode) {
                case OF_BLENDMODE_ALPHA:
                    //ofSetColor(255, 255, 255, 255 / totalVisible);
                    break;
                default:
                    ofSetColor(255);
                    break;
            }
            ofEnableBlendMode(data.blendMode);
            draw(getVarVec3("pos"), data.getSize());
            ofDisableBlendMode();
        }
        data.afterDraw(vars);
    }
}

void Layer::doScale() {
    glm::vec3 scale = getVarVec3("scale");
    if (scale.x < 0) {
        ofTranslate(-scale.x * data.getSize().x, 0);
    }
    if (scale.y < 0) {
        ofTranslate(0, -scale.y * data.getSize().y);
    }
    if (scale.z < 0) {
        ofTranslate(0, 0, -scale.z);
    }
    ofScale(scale);
}

void Layer::doAlign() {
    glm::vec3 scale = getVarVec3("scale");
    glm::vec2 size = data.getSize();
    switch (alignH) {
        case OF_ALIGN_HORZ_CENTER:
            ofTranslate(ofGetWidth()/2.f - size.x * abs(scale.x) / 2.f, 0);
            break;
        case OF_ALIGN_HORZ_RIGHT:
            ofTranslate(ofGetWidth() - size.x * abs(scale.x), 0);
            break;
    }
    switch (alignV) {
        case OF_ALIGN_VERT_CENTER:
            ofTranslate(0, ofGetHeight()/2.f - size.y * abs(scale.y) / 2.f);
            break;
        case OF_ALIGN_VERT_BOTTOM:
            ofTranslate(0, ofGetHeight() - size.y * abs(scale.y));
            break;
    }
}

void Layer::doRotate() {
    glm::vec3 rotation = getVarVec3("rotation");
    glm::vec3 scale = getVarVec3("scale");
    float degrees = glm::length(rotation);
    if (degrees != 0) {
        glm::vec3 axis = glm::normalize(rotation);
        glm::vec3 pivot = getVarVec3("pivot");
        glm::vec2 size = data.getSize();
        ofTranslate(pivot.x * size.x * abs(scale.x), pivot.y * size.y * abs(scale.y));
        ofRotateDeg(degrees, axis.x, axis.y, axis.z);
        ofTranslate(-pivot.x * size.x * abs(scale.x), -pivot.y * size.y * abs(scale.y));
    }
}

void Layer::unload() {
    geom = NULL;
    GeomPool::clean(_id);
    glm::vec3 pos = getVarVec3("pos", glm::vec3());
    glm::vec3 size = getVarVec3("size", glm::vec3());
    vars.clear();
    VariablePool::cleanup(this);
    setVar("pos", pos);
    setVar("size", size);
}

void Layer::reset() {
    unload();
    shader.reset();
    setVar("visible", true);
    setVar("speed", 1.f);
    setVar("tint", ofFloatColor(1.f, 1.f));
    resetTransform();
    setVar("visibleThresh", 1.f);
    setVar("onsetThresh", 1.f);
}

void Layer::resetTransform() {
    setVar("alpha", 1.f);
    setVar("bri", 1.f);
    setVar("rotation", glm::vec3(0, 0, 0));
    setVar("pivot", glm::vec3(0, 0, 0));
    setVar("scale", glm::vec3(1));
}
