#include "ofMain.h"
#include "ofApp.h"
#include "Config.h"

#define USE_PROGRAMMABLE_RENDERER

#ifdef USE_PROGRAMMABLE_RENDERER
#include "ofGLProgrammableRenderer.h"
#endif

//========================================================================
int main( ){
    
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
