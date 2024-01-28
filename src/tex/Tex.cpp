#include "Tex.h"
#include "ShaderTex.h"
#include "ShaderPingPongTex.h"
#include "VideoTex.h"
#include "SketchTex.h"
#include "ImageTex.h"
#include "HPVideoTex.h"
//#include "GVVideoTex.h"
#include "WebcamTex.h"
#include "DrawTex.h"
#include "Config.h"
#include "VariablePool.h"

#if USE_ULTRALIGHT
#include "UltralightTex.h"
#endif

shared_ptr<Tex> Tex::factory(string type, string path, const vector<float>& args) {
    shared_ptr<Tex> tex = NULL;
    auto it = SourceMap.find(type);
    if (it != SourceMap.end()) {
        switch (it->second) {
#if USE_ULTRALIGHT
            case Source::HTML:
                tex = make_shared<UltralightTex>(path, args);
                break;
#endif
            case Source::VIDEO:
                tex = make_shared<VideoTex>(path, args);
                break;
            case Source::HPV:
                tex = make_shared<HPVideoTex>(path, args);
                break;
//            case Source::GV:
//                tex = make_shared<GVVideoTex>(path, args);
//                break;
            case Source::SHADER:
                tex = make_shared<ShaderTex>(path, args);
                break;
            case Source::SHADER_PP:
                tex = make_shared<ShaderPingPongTex>(path, args);
                break;
            case Source::SKETCH:
                tex = make_shared<SketchTex>(path, args);
                break;
            case Source::IMAGE:
                tex = make_shared<ImageTex>(path, args);
                break;
            case Source::WEBCAM:
                tex = make_shared<WebcamTex>(path, args);
                break;
            case Source::DRAW:
                tex = make_shared<DrawTex>(path, args);
                break;
            case Source::COLOR:
                tex = make_shared<ShaderTex>(path, args);
                auto shaderTex = dynamic_pointer_cast<ShaderTex>(tex);
                shaderTex->setVar("color", ofFloatColor(args[0], args[1], args[2], args[3]));
                break;
        }
    }
    return tex;
}

shared_ptr<Tex> Tex::factory(string source, const vector<float>& args) {
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
                case Source::HTML:
                    // todo: use google's "I'm feeling lucky"
                    ofLogError() << "/tex/choose for html not implemented";
                    break;
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
                    auto tex = factory("shader", "color", args);
                    auto shaderTex = dynamic_pointer_cast<ShaderTex>(tex);
                    shaderTex->setVar("color", ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f), ofRandom(1.f)));
                    return tex;
                }
            }
        }
    }
    if (path != "") {
        return factory(type, path, args);
    }
    return NULL;
}
