#include "Tex.h"
#include "ShaderTex.h"
#include "ShaderPingPongTex.h"
#include "VideoTex.h"
#include "SketchTex.h"
#include "ImageTex.h"
#include "HPVideoTex.h"
#include "WebcamTex.h"
#include "DrawTex.h"
#include "Config.h"

Tex* Tex::factory(string type, string path, const vector<float>& args) {
    Tex *tex = NULL;
    auto it = SourceMap.find(type);
    if (it != SourceMap.end()) {
        switch (it->second) {
            case Source::VIDEO:
                tex = new VideoTex(path, args);
                break;
            case Source::HPV:
                tex = new HPVideoTex(path, args);
                break;
            case Source::SHADER:
                tex = new ShaderTex(path, args);
                break;
            case Source::SHADER_PP:
                tex = new ShaderPingPongTex(path, args);
                break;
            case Source::SKETCH:
                tex = new SketchTex(path, args);
                break;
            case Source::IMAGE:
                tex = new ImageTex(path, args);
                break;
            case Source::WEBCAM:
                tex = new WebcamTex(path, args);
                break;
            case Source::DRAW:
                tex = new DrawTex(path, args);
                break;
            case Source::COLOR:
                tex = new ShaderTex(path, args);
                dynamic_cast<ShaderTex*>(tex)->setUniform("color", args);
                break;
        }
    }
    return tex;
}

Tex* Tex::factory(string source, const vector<float>& args) {
    string type = source;
    string path = "";
    bool explicitType = source.find(":") != string::npos;
    if (explicitType) {
        type = source.substr(0, source.find(":"));
        path = source.substr(source.find(":") + 1);
    }
    if (path == "") {
        auto it = SourceMap.find(type);
        if (it != SourceMap.end()) {
            switch (it->second) {
                case Source::VIDEO:
                    path = VideoTex::random();
                    break;
                case Source::HPV:
                    path = HPVideoTex::random();
                    break;
                case Source::SHADER:
                    path = ShaderTex::random();
                    break;
                case Source::SHADER_PP:
                    path = ShaderPingPongTex::random();
                    break;
                case Source::SKETCH:
                    path = SketchTex::random();
                    break;
                case Source::IMAGE:
                    path = ImageTex::random();
                    break;
                case Source::WEBCAM:
                    path = WebcamTex::random();
                    break;
                case Source::DRAW:
                    path = DrawTex::random();
                    break;
                case Source::COLOR: {
                    Tex* tex = factory("shader", "color", args);
                    ofFloatColor color = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
                    dynamic_cast<ShaderTex*>(tex)->setUniform("color", color);
                    return tex;
                }
            }
        }
    }
    return factory(type, path, args);
}
