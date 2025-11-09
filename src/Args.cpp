#include "Args.h"
#include "ColorUtil.h"
#include "ofxExpr.hpp"
#include <regex>

bool Args::isHexColor(const string& str) {
    return (str.substr(0, 1) == "#" && str.size() == 7) || (str.substr(0, 2) == "0x" && str.size() == 8);
}

bool Args::isURL(const string& str) {
    std::regex re("^https?://.+");
    return regex_match(str, re);
}

bool Args::isJSON(const string& str) {
    return isJSONObj(str) || isJSONArr(str);
}

bool Args::isJSONObj(const string& str) {
    return str.substr(0, 1) == "{";
}

bool Args::isJSONArr(const string& str) {
    return str.substr(0, 1) == "[";
}

ofFloatColor Args::parseHexColor(const string& str) {
    string hex = str.size() == 7 ? str.substr(1) : str;
    return ofFloatColor::fromHex(ofHexToInt(hex));
}

float Args::parsePercent(const ofxOscMessage& m, int idx) {
    return m.getArgType(idx) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(idx) > 1.f ? m.getArgAsFloat(idx) / 100.f : m.getArgAsFloat(idx);
}

template<>
float Args::parse(const ofxOscMessage& m, int idx) {
    float value;
    switch (m.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            string str = m.getArgAsString(idx);
            value = ofToFloat(str);
            if (isnan(value) || strspn(str.c_str(), "-.0123456789") != str.size()) {
                throw "string not float: " + str;
            }
            break;
        }
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_CHAR:
            value = (float) m.getArgAsInt(idx);
            break;
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            value = m.getArgAsFloat(idx);
            break;
        default:
            throw "could not parse float at: " + ofToString(idx);
    }
    return value;
}

template<>
ofFloatColor Args::parse(const ofxOscMessage& m, int idx) {
    ofFloatColor color;
    switch (m.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            const string str = m.getArgAsString(idx);
            if (isHexColor(str)) {
                color = parseHexColor(str);
            }
//            else if (str.substr(0, 5) == "rgba(") {
//
//            }
//            else if (str.substr(0, 4) == "rgb(") {
//
//            }
            else if (isJSON(str)) {
                const auto json = ofJson::parse(str);
                size_t size = 3;
                if (isJSONObj(str)) {
                    color.r = json.at("r");
                    color.g = json.at("g");
                    color.b = json.at("b");
                    color.a = 1.f;
                    if (json.find("a") != json.end()) {
                        color.a = json.at("a");
                        size = 4;
                    }
                }
                else if (isJSONArr(str)) {
                    color.r = json[0];
                    color.g = json[1];
                    color.b = json[2];
                    color.a = 1.f;
                    if (json.size() > 3) {
                        color.a = json[3];
                        size = 4;
                    }
                }
                if (color.r > 1.f || color.g > 1.f || color.b > 1.f || color.a > 1.f) {
                    color.r = color.r / 255.f;
                    color.g = color.g / 255.f;
                    color.b = color.b / 255.f;
                    if (size > 3) {
                        color.a = color.a / 255.f;
                    }
                }
            }
            else {
                throw ("string not color: " + str);
            }
            break;
        }
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_CHAR:
            color = ofColor(m.getArgAsInt(idx));
            break;
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            color = ofFloatColor(m.getArgAsFloat(idx));
            break;
        case OFXOSC_TYPE_RGBA_COLOR:
            color = m.getArgAsRgbaColor(idx);
            break;
        default:
            throw "could not parse color at: " + ofToString(idx);
    }
    return color;
}

template<>
glm::vec3 Args::parse(const ofxOscMessage &m, int idx) {
    glm::vec3 vec;
    switch (m.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            string str = m.getArgAsString(idx);
            if (isJSONObj(str)) {
                auto json = ofJson::parse(str);
                vec.x = json.at("x");
                vec.y = json.at("y");
                vec.z = json.at("z");
            }
            else if (isJSONArr(str)) {
                auto json = ofJson::parse(str);
                vec.x = json[0];
                vec.y = json[1];
                vec.z = json[2];
            }
            else {
                throw ("string not vec3: " + str);
            }
            break;
        }
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_CHAR:
            vec = glm::vec3(m.getArgAsInt(idx));
            break;
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            vec = glm::vec3(m.getArgAsFloat(idx));
            break;
        default:
            throw "could not parse vec3 at: " + ofToString(idx);
    }
    return vec;
}

template<>
glm::mat4 Args::parse(const ofxOscMessage &m, int idx) {
    glm::mat4 mat;
    throw "Args::parse<glm::mat4> not implemented";
    return mat;
}

template<>
ofxExprNode Args::parse(const ofxOscMessage &m, int idx) {
    ofxExprNode node;
    throw "Args::parse<ofxNodeExpr> not implemented";
//    switch (m.getArgType(idx)) {
//        case OFXOSC_TYPE_STRING: {
//            string str = m.getArgAsString(idx);
//            if (isJSONObj(str)) {
//                auto json = ofJson::parse(str);
//                vec.x = json.at("r");
//                vec.y = json.at("g");
//                vec.z = json.at("b");
//            }
//            else if (isJSONArr(str)) {
//                auto json = ofJson::parse(str);
//                vec.x = json[0];
//                vec.y = json[1];
//                vec.z = json[2];
//            }
//            else {
//                throw ("string not vec3: " + str);
//            }
//            break;
//        }
//        case OFXOSC_TYPE_INT32:
//        case OFXOSC_TYPE_INT64:
//        case OFXOSC_TYPE_CHAR:
//            vec = glm::vec3(m.getArgAsInt(idx));
//            break;
//        case OFXOSC_TYPE_FLOAT:
//        case OFXOSC_TYPE_DOUBLE:
//            vec = glm::vec3(m.getArgAsFloat(idx));
//            break;
//        default:
//            throw "could not parse vec3 at: " + ofToString(idx);
//    }
    return node;
}

template<typename T>
vector<T> Args::parseConst(const ofxOscMessage &m, int idx) {
    vector<T> values(m.getNumArgs()-idx);
    for (int i=idx; i<m.getNumArgs(); i++) {
        values[i-idx] = parse<T>(m, i);
    }
    return values;
}

template<>
vector<string> Args::parseExpr<float>(const ofxOscMessage& m, int& idx) {
    vector<string> floatExpr;
    switch (m.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            floatExpr.push_back(m.getArgAsString(idx++));
            break;
        }
        default:
            throw "could not parse float expr: " + ofToString(idx);
    }
    return floatExpr;
}

template<>
vector<string> Args::parseExpr<ofFloatColor>(const ofxOscMessage& m, int& idx) {
    vector<string> colorExpr;
    colorExpr.resize(4);
    switch (m.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            string str = m.getArgAsString(idx++);
            if (isJSONArr(str)) {
                auto json = ofJson::parse(str);
                if (json.size() > 0) {
                    if (json.at(0).is_string())
                        colorExpr[0] = json.at(0);
                    else
                        colorExpr[0] = ofToString(json.at(0));
                }
                if (json.size() > 1) {
                    if (json.at(1).is_string())
                        colorExpr[1] = json.at(1);
                    else
                        colorExpr[1] = ofToString(json.at(1));
                }
                if (json.size() > 2) {
                    if (json.at(2).is_string())
                        colorExpr[2] = json.at(2);
                    else
                        colorExpr[2] = ofToString(json.at(2));
                }
                colorExpr[3] = "1.0";
                if (json.size() > 3) {
                    if (json.at(3).is_string())
                        colorExpr[3] = json.at(3);
                    else
                        colorExpr[3] = ofToString(json.at(3));
                }
            }
            else if (isJSONObj(str)) {
                auto json = ofJson::parse(str);
                colorExpr[0] = json.at("r");
                colorExpr[1] = json.at("g");
                colorExpr[2] = json.at("b");
                colorExpr[3] = json.at("a");
            }
            else {
                colorExpr[0] = str;
                colorExpr[1] = str;
                colorExpr[2] = str;
                colorExpr[3] = str;
            }
            break;
        }
        case OFXOSC_TYPE_BLOB:
            // todo: implement
            // m.getArgAsBlob(idx++);
            break;
        default:
            throw "could not parse vec3 at: " + ofToString(idx);
    }
    return colorExpr;
}

template<>
vector<string> Args::parseExpr<glm::vec3>(const ofxOscMessage &m, int& idx) {
    vector<string> vec3Expr;
    vec3Expr.resize(3);
    switch (m.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            string str = m.getArgAsString(idx++);
            if (isJSONArr(str)) {
                auto json = ofJson::parse(str);
                if (json.at(0).is_string())
                    vec3Expr[0] = json.at(0);
                else
                    vec3Expr[0] = ofToString(json.at(0));
                if (json.at(1).is_string())
                    vec3Expr[1] = json.at(1);
                else
                    vec3Expr[1] = ofToString(json.at(1));
                if (json.at(2).is_string())
                    vec3Expr[2] = json.at(2);
                else
                    vec3Expr[2] = ofToString(json.at(2));
            }
            else if (isJSONObj(str)) {
                auto json = ofJson::parse(str);
                if (json.at("x").is_string())
                    vec3Expr[0] = json.at("x");
                else
                    vec3Expr[0] = ofToString(json.at("x"));
                if (json.at("y").is_string())
                    vec3Expr[0] = json.at("y");
                else
                    vec3Expr[0] = ofToString(json.at("y"));
                if (json.at("z").is_string())
                    vec3Expr[0] = json.at("z");
                else
                    vec3Expr[0] = ofToString(json.at("z"));
            }
            else {
                vec3Expr[0] = str;
                vec3Expr[1] = str;
                vec3Expr[2] = str;
            }
            break;
        }
        case OFXOSC_TYPE_BLOB:
            // todo: implement
            // m.getArgAsBlob(idx++);
            break;
        default:
            ofLogError() << "could not parse vec3 expr: " << m << idx;
            break;
    }
    return vec3Expr;
}

template<>
vector<string> Args::parseExpr<glm::mat4>(const ofxOscMessage &m, int& idx) {
    ofLogError() << "Args::parseExpr<glm::mat4> not implemented";
    vector<string> mat4Expr;
    mat4Expr.resize(16);
    return mat4Expr;
}

template<>
vector<string> Args::parseExpr<ofxExprNode>(const ofxOscMessage &m, int& idx) {
    vector<string> nodeExpr;
    nodeExpr.resize(9);
    vector<string> pos = Args::parseExpr<glm::vec3>(m, idx);
    nodeExpr[0] = pos[0];
    nodeExpr[1] = pos[1];
    nodeExpr[2] = pos[2];
    vector<string> rota = Args::parseExpr<glm::vec3>(m, idx);
    nodeExpr[3] = rota[0];
    nodeExpr[4] = rota[1];
    nodeExpr[5] = rota[2];
    vector<string> scale = Args::parseExpr<glm::vec3>(m, idx);
    nodeExpr[6] = scale[0];
    nodeExpr[7] = scale[1];
    nodeExpr[8] = scale[2];
    return nodeExpr;
}

template vector<float> Args::parseConst<float>(const ofxOscMessage& m, int idx);
template vector<ofFloatColor> Args::parseConst<ofFloatColor>(const ofxOscMessage& m, int idx);
template vector<glm::vec3> Args::parseConst<glm::vec3>(const ofxOscMessage& m, int idx);
template vector<glm::mat4> Args::parseConst<glm::mat4>(const ofxOscMessage& m, int idx);
template vector<ofxExprNode> Args::parseConst<ofxExprNode>(const ofxOscMessage& m, int idx);
