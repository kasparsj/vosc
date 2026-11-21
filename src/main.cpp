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
int main( ){
    // Reject invalid startup configuration before creating a window or OSC receiver.
    try {
        control::oscPortFromEnvironment();
    } catch (const std::invalid_argument& error) {
        std::cerr << error.what() << std::endl;
        return EXIT_FAILURE;
    }
    
#ifdef USE_PROGRAMMABLE_RENDERER
    ofGLWindowSettings settings;
    settings.setGLVersion(GL_VERSION_MAJOR, GL_VERSION_MINOR);
#ifdef GL_PROFILE
    settings.setProfile(GL_PROFILE);
#endif
    settings.setSize(1024, 768);
    ofCreateWindow(settings);
#else
    ofSetupOpenGL(1024, 768, OF_WINDOW);
#endif
    
    ofRunApp(new ofApp());

}
