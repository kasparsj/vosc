#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){
    ofGLWindowSettings s;
    s.setGLVersion(4, 1);
    s.setSize(1920/2, 1080);
    ofCreateWindow(s);
    
	ofRunApp(new ofApp());

}
