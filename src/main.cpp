#include "ofMain.h"
#include "ofApp.h"

#define USE_PROGRAMMABLE_RENDERER

#ifdef USE_PROGRAMMABLE_RENDERER
#include "ofGLProgrammableRenderer.h"
#endif

//========================================================================
int main( ){
    
#ifdef USE_PROGRAMMABLE_RENDERER
    ofGLWindowSettings settings;
    settings.setGLVersion(3, 2);
    settings.setSize(1024, 768);
    ofCreateWindow(settings);
#else
    ofSetupOpenGL(1024, 768, OF_WINDOW);
#endif
    
    ofRunApp(new ofApp());

}
