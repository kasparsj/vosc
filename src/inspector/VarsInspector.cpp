#include "VarsInspector.h"
#include "../var/Variable.h"

VarsInspector::VarsInspector() {
}

void VarsInspector::draw(const VarsHolder* holder, int x, int y) {
    if (holder == nullptr) {
        return;
    }
    draw(holder->vars, x, y);
}

void VarsInspector::draw(const map<string, shared_ptr<BaseVar>>& vars, int x, int y) {
    ofSetColor(255);
    ofDrawBitmapString("Variables:", x, y);
    
    if (vars.empty()) {
        ofSetColor(128);
        ofDrawBitmapString("  No variables", x, y + 20);
        return;
    }
    
    int currentY = y + 20;
    for (const auto& it : vars) {
        const string& name = it.first;
        const shared_ptr<BaseVar>& var = it.second;
        
        string displayStr = "  " + name + ": " + formatVariableValue(var);
        
        ofSetColor(255);
        ofDrawBitmapString(displayStr, x, currentY);
        currentY += 15;
        
        // Limit display to prevent overflow
        if (currentY > ofGetHeight() - 50) {
            ofDrawBitmapString("  ... (more variables)", x, currentY);
            break;
        }
    }
}

string VarsInspector::formatVariableValue(const shared_ptr<BaseVar>& var) {
    // Try to get value based on type
    const Variable<float>* floatVar = dynamic_cast<const Variable<float>*>(var.get());
    const Variable<int>* intVar = dynamic_cast<const Variable<int>*>(var.get());
    const Variable<glm::vec2>* vec2Var = dynamic_cast<const Variable<glm::vec2>*>(var.get());
    const Variable<glm::vec3>* vec3Var = dynamic_cast<const Variable<glm::vec3>*>(var.get());
    const Variable<glm::vec4>* vec4Var = dynamic_cast<const Variable<glm::vec4>*>(var.get());
    const Variable<ofFloatColor>* colorVar = dynamic_cast<const Variable<ofFloatColor>*>(var.get());
    
    if (floatVar != nullptr) {
        const vector<float>& values = floatVar->getVec();
        if (values.size() == 1) {
            return ofToString(values[0]);
        } else {
            string result = "[";
            for (size_t i = 0; i < values.size(); i++) {
                if (i > 0) result += ", ";
                result += ofToString(values[i]);
            }
            result += "]";
            return result;
        }
    }
    else if (intVar != nullptr) {
        const vector<int>& values = intVar->getVec();
        if (values.size() == 1) {
            return ofToString(values[0]);
        } else {
            string result = "[";
            for (size_t i = 0; i < values.size(); i++) {
                if (i > 0) result += ", ";
                result += ofToString(values[i]);
            }
            result += "]";
            return result;
        }
    }
    else if (vec2Var != nullptr) {
        glm::vec2 val = vec2Var->get();
        return "vec2(" + ofToString(val.x) + ", " + ofToString(val.y) + ")";
    }
    else if (vec3Var != nullptr) {
        glm::vec3 val = vec3Var->get();
        return "vec3(" + ofToString(val.x) + ", " + ofToString(val.y) + ", " + ofToString(val.z) + ")";
    }
    else if (vec4Var != nullptr) {
        glm::vec4 val = vec4Var->get();
        return "vec4(" + ofToString(val.x, 3) + ", " + ofToString(val.y, 3) + ", " + ofToString(val.z, 3) + ", " + ofToString(val.w, 3) + ")";
    }
    else if (colorVar != nullptr) {
        ofFloatColor val = colorVar->get();
        return "color(" + ofToString(val.r) + ", " + ofToString(val.g) + ", " + ofToString(val.b) + ", " + ofToString(val.a) + ")";
    }
    else {
        return "?";
    }
}

