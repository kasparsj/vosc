#include "BaseVar.h"
#include "Variable.h"
#include "Args.h"
#include "ColorWheelSchemes.h"

using namespace ofxColorTheory;

typedef ColorWheelScheme_<ofFloatColor> FloatColorWheelScheme;
typedef ColorWheelSchemes_<ofFloatColor> FloatColorWheelSchemes;

template<>
const std::vector<std::shared_ptr<ColorWheelScheme_<ofFloatColor>>> ColorWheelSchemes_<ofFloatColor>::SCHEMES = ColorWheelSchemes_<ofFloatColor>::createColorSchemes();

shared_ptr<BaseVar> BaseVar::create(const ofxOscMessage& m, int idx) {
    string command = m.getAddress();
    return create(command, m, idx);
}

shared_ptr<BaseVar> BaseVar::create(const string& command, const ofxOscMessage& m, int idx) {
    if (command.length() >= 4 && command.substr(command.length()-4) == "/var") {
        return createVar2(m, idx);
    }
    else if (command.length() >= 11 && command.substr(command.length()-11) == "/var/colors") {
        shared_ptr<Variable<ofFloatColor>> var = make_shared<Variable<ofFloatColor>>();
        var->set(m, idx);
        return var;
    }
    else if (command.length() >= 18 && command.substr(command.length()-18) == "/var/colors/scheme") {
        shared_ptr<Variable<ofFloatColor>> var = make_shared<Variable<ofFloatColor>>();
        updateColorsScheme(var, m, idx);
        return var;
    }
    ofLogError() << "VariablePool::create command not recognized: " << m;
    return NULL;
}

shared_ptr<BaseVar> BaseVar::createVar(const ofxOscMessage& m, int idx, size_t size) {
    if (size == 3) {
        Variable<glm::vec3>* var = new Variable<glm::vec3>();
        var->set(m, idx);
        return shared_ptr<BaseVar>(var);
    }
    else if (size == 4) {
        Variable<ofFloatColor>* var = new Variable<ofFloatColor>();
        var->set(m, idx);
        return shared_ptr<BaseVar>(var);
    }
    else if (size == 16) {
        Variable<glm::mat4>* var = new Variable<glm::mat4>();
        var->set(m, idx);
        return shared_ptr<BaseVar>(var);
    }
    // todo: support vector<float>
    ofLogError() << "VariablePool::create not implemented for size: " << size;
    return NULL;
}

shared_ptr<BaseVar> BaseVar::createVar(const ofxOscMessage& m, int idx) {
    ofxOscArgType type = m.getArgType(idx);
    switch (type) {
        case OFXOSC_TYPE_STRING: {
            string str = m.getArgAsString(idx);
            if (Args::isJSON(str)) {
                auto json = ofJson::parse(str);
                return createVar(m, idx, json.size());
            }
            else {
                shared_ptr<Variable<float>> var = make_shared<Variable<float>>();
                var->set(m, idx);
                return var;
            }
        }
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_TRUE:
        case OFXOSC_TYPE_FALSE: {
            shared_ptr<Variable<float>> var = make_shared<Variable<float>>();
            var->set(m, idx);
            return var;
        }
        case OFXOSC_TYPE_BLOB: {
            ofBuffer buf = m.getArgAsBlob(idx);
            return createVar(m, idx, buf.size());
        }
        default:
            ofLogError() << "VariablePool::create type not implemented: " << type << m << idx;
            return NULL;
    }
}

shared_ptr<BaseVar> BaseVar::createVar2(const ofxOscMessage& m, int idx) {
    if (m.getNumArgs() > idx + 1) {
        shared_ptr<Variable<ofxExprNode>> var = make_shared<Variable<ofxExprNode>>();
        var->set(m, idx);
        return var;
    }
    else {
        return createVar(m, idx);
    }
}

void BaseVar::update(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx) {
    const string& command = m.getAddress();
    update(var, command, m, idx);
}

void BaseVar::update(shared_ptr<BaseVar>& var, const string& command, const ofxOscMessage& m, int idx) {
    if (command.length() >= 4 && command.substr(command.length()-4) == "/var") {
        updateVar(var, m, idx);
    }
    else if (command.length() >= 11 && command.substr(command.length()-11) == "/var/colors") {
        static_cast<Variable<ofFloatColor>*>(var.get())->set(m, idx);
    }
    else if (command.length() >= 18 && command.substr(command.length()-18) == "/var/colors/scheme") {
        updateColorsScheme(var, m, idx);
    }
}

void BaseVar::updateVar(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx, size_t size) {
    if (size == 3) {
        auto var1 = dynamic_pointer_cast<Variable<glm::vec3>>(var);
        if (var1) {
            var1->set(m, idx);
            return;
        }
        auto var2 = dynamic_pointer_cast<Variable<ofFloatColor>>(var);
        if (var2) {
            var2->set(m, idx);
            return;
        }
    }
    else if (size == 4) {
        auto var3 = dynamic_pointer_cast<Variable<ofFloatColor>>(var);
        if (var3) {
            var3->set(m, idx);
            return;
        }
    }
    else if (size == 16) {
        auto var4 = dynamic_pointer_cast<Variable<glm::mat4>>(var);
        if (var4) {
            var4->set(m, idx);
            return;
        }
    }
    // todo: support vector<float>
    ofLogError() << "VariablePool::create not implemented for size: " << size;
}

void BaseVar::updateVar(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx) {
    ofxOscArgType type = m.getArgType(idx);
    switch (type) {
        case OFXOSC_TYPE_STRING: {
            string str = m.getArgAsString(idx);
            if (Args::isJSON(str)) {
                auto json = ofJson::parse(str);
                updateVar(var, m, idx, json.size());
                return;
            }
            break;
        }
        case OFXOSC_TYPE_BLOB: {
            ofBuffer buf = m.getArgAsBlob(idx);
            updateVar(var, m, idx, buf.size());
            return;
        }
    }
    auto var1 = dynamic_cast<Variable<float>*>(var.get());
    if (var1 != NULL) {
        var1->set(m, idx);
        return;
    }
    auto var2 = dynamic_cast<Variable<ofFloatColor>*>(var.get());
    if (var2 != NULL) {
        var2->set(m, idx);
        return;
    }
    auto var3 = dynamic_cast<Variable<glm::vec3>*>(var.get());
    if (var3 != NULL) {
        var3->set(m, idx);
        return;
    }
    ofLogError() << "VariablePool::update type not implemented: " << type << m << idx;
}

void BaseVar::updateColorsScheme(const shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx) {
    string schemeName = m.getArgAsString(idx);
    ofFloatColor primaryColor = Args::parse<ofFloatColor>(m, idx+1);
    shared_ptr<FloatColorWheelScheme> scheme = FloatColorWheelSchemes::get(schemeName);
    if (scheme != NULL) {
        scheme->setPrimaryColor(primaryColor);
        int numColors = m.getNumArgs() > (idx+2) ? m.getArgAsInt(idx+2) : 1;
        static_cast<Variable<ofFloatColor>*>(var.get())->set(scheme->interpolate(numColors));
    }
    else {
        ofLogError() << "/var/colors/scheme invalid scheme name: " << schemeName;
    }

}
