import qbs
import qbs.Process
import qbs.File
import qbs.FileInfo
import qbs.TextFile
import "../../../libs/openFrameworksCompiled/project/qtcreator/ofApp.qbs" as ofApp

Project{
    property string of_root: "../../.."

    ofApp {
        name: { return FileInfo.baseName(sourceDirectory) }

        files: [
            'src/Args.cpp',
            'src/Args.h',
            'src/Config.h',
            'src/FboPingPong.cpp',
            'src/FboPingPong.h',
            'src/Geom.cpp',
            'src/Geom.h',
            'src/GeomPool.cpp',
            'src/GeomPool.h',
            'src/Lights.cpp',
            'src/Lights.h',
            'src/Shader.cpp',
            'src/Shader.h',
            'src/Sketch.h',
            'src/Texture.cpp',
            'src/Texture.h',
            'src/TexturePool.cpp',
            'src/TexturePool.h',
            'src/layer/Layer.cpp',
            'src/layer/Layer.h',
            'src/layer/LayerData.cpp',
            'src/layer/LayerData.h',
            'src/layer/LayerShader.cpp',
            'src/layer/LayerShader.h',
            'src/main.cpp',
            'src/ofApp.cpp',
            'src/ofApp.h',
            'src/sketches/NoisySpirals.h',
            'src/sketches/Spiral.h',
            'src/sketches/WaveClock.h',
            'src/sound/Mic.cpp',
            'src/sound/Mic.h',
            'src/sound/Sound.cpp',
            'src/sound/Sound.h',
            'src/tex/DrawTex.cpp',
            'src/tex/DrawTex.h',
            'src/tex/FboPingPongTex.cpp',
            'src/tex/FboPingPongTex.h',
            'src/tex/FboTex.cpp',
            'src/tex/FboTex.h',
            'src/tex/HPVideoTex.cpp',
            'src/tex/HPVideoTex.h',
            'src/tex/ImageTex.cpp',
            'src/tex/ImageTex.h',
            'src/tex/ShaderPingPongTex.cpp',
            'src/tex/ShaderPingPongTex.h',
            'src/tex/ShaderTex.cpp',
            'src/tex/ShaderTex.h',
            'src/tex/SketchTex.cpp',
            'src/tex/SketchTex.h',
            'src/tex/Tex.cpp',
            'src/tex/Tex.h',
            'src/tex/TexData.cpp',
            'src/tex/TexData.h',
            'src/tex/VideoTex.cpp',
            'src/tex/VideoTex.h',
            'src/tex/WebcamTex.cpp',
            'src/tex/WebcamTex.h',
            'src/var/Value.cpp',
            'src/var/Value.h',
            'src/var/Variable.cpp',
            'src/var/Variable.h',
            'src/var/VariablePool.cpp',
            'src/var/VariablePool.h',
            'src/var/VarsHolder.cpp',
            'src/var/VarsHolder.h',
        ]

        of.addons: [
            'ofxAssimpModelLoader',
            'ofxAutoReloadedShader',
            'ofxColorTheory',
            'ofxEasing',
            'ofxHPVPlayer',
            'ofxImGui',
            'ofxLooper',
            'ofxMidi',
            'ofxOsc',
            'ofxPostProcessing',
            'ofxTidalCycles',
        ]

        // additional flags for the project. the of module sets some
        // flags by default to add the core libraries, search paths...
        // this flags can be augmented through the following properties:
        of.pkgConfigs: []       // list of additional system pkgs to include
        of.includePaths: []     // include search paths
        of.cFlags: []           // flags passed to the c compiler
        of.cxxFlags: []         // flags passed to the c++ compiler
        of.linkerFlags: []      // flags passed to the linker
        of.defines: []          // defines are passed as -D to the compiler
                                // and can be checked with #ifdef or #if in the code
        of.frameworks: []       // osx only, additional frameworks to link with the project
        of.staticLibraries: []  // static libraries
        of.dynamicLibraries: [] // dynamic libraries

        // other flags can be set through the cpp module: http://doc.qt.io/qbs/cpp-module.html
        // eg: this will enable ccache when compiling
        //
        // cpp.compilerWrapper: 'ccache'

        Depends{
            name: "cpp"
        }

        // common rules that parse the include search paths, core libraries...
        Depends{
            name: "of"
        }

        // dependency with the OF library
        Depends{
            name: "openFrameworks"
        }
    }

    property bool makeOF: true  // use makfiles to compile the OF library
                                // will compile OF only once for all your projects
                                // otherwise compiled per project with qbs
    

    property bool precompileOfMain: false  // precompile ofMain.h
                                           // faster to recompile when including ofMain.h 
                                           // but might use a lot of space per project

    references: [FileInfo.joinPaths(of_root, "/libs/openFrameworksCompiled/project/qtcreator/openFrameworks.qbs")]
}
