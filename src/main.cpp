#include "ofMain.h"
#include "ofApp.h"
#include "Config.h"
#include "control/OscPort.hpp"
#include <iostream>

#define USE_PROGRAMMABLE_RENDERER

#ifdef USE_PROGRAMMABLE_RENDERER
#include "ofGLProgrammableRenderer.h"
#endif

//========================================================================
int main(int argc, char** argv) {
    constexpr auto usage = "Usage: vosc [--scene <path/to/scene.vosc.json>]";
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << usage << std::endl;
        return EXIT_SUCCESS;
    }
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--scene" || !argv[2][0])) {
        std::cerr << usage << std::endl;
        return EXIT_FAILURE;
    }
    unsigned int oscPort;
    std::optional<scene::File> startupScene;
    try {
        oscPort = control::oscPortFromEnvironment();
        if (argc == 3) {
            startupScene = scene::load(argv[2]);
            CommandSystem::validateStartupScene(*startupScene);
        }
    } catch (const std::exception& error) {
        std::cerr << "vosc: ";
        if (argc == 3) std::cerr << argv[2] << ": ";
        std::cerr << error.what() << std::endl;
        return EXIT_FAILURE;
    }
    const int width = startupScene ? startupScene->width : 1024;
    const int height = startupScene ? startupScene->height : 768;
#ifdef VOSC_RECOVERY_TESTING
    CommandSystem::traceStartupBoundary("window");
#endif

#ifdef USE_PROGRAMMABLE_RENDERER
    ofGLWindowSettings settings;
    settings.setGLVersion(GL_VERSION_MAJOR, GL_VERSION_MINOR);
#ifdef GL_PROFILE
    settings.setProfile(GL_PROFILE);
#endif
    settings.setSize(width, height);
    ofCreateWindow(settings);
#else
    ofSetupOpenGL(width, height, OF_WINDOW);
#endif
    
    bool startupFailed = false;
    const int status = ofRunApp(new ofApp(oscPort, std::move(startupScene), startupFailed));
    if (startupFailed) return EXIT_FAILURE;
    return argc == 1 ? EXIT_SUCCESS : status;

}
