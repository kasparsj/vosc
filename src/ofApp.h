#pragma once

#include "ofMain.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "Visual.h"
#include "Config.h"

class ofApp : public ofBaseApp {

public:
	void setup();
    void setupSounds(int numInsts);
    void setupVisuals(int numVisuals, Layout layout);
	void update();
    void parseMessages();
    void parseMessage(const ofxOscMessage &m);
    void visualCommand(Visual &visual, string command, const ofxOscMessage &m);
	void draw();
	void exit();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);

    ofxOscReceiver receiver;
    Config config;
	ofxTidalCycles *tidal;
    
    vector<Sound> sounds;
    vector<Visual> visuals;
    
    ofFbo fbo;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    ofBlendMode bgBlendMode = OF_BLENDMODE_ALPHA;
    ofColor bgColor = ofColor(0);
    bool showDebug = true;
};
