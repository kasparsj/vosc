#pragma once

#include "ofMain.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "SoundData.h"
#include "Visual.h"
#include "Config.h"

class ofApp : public ofBaseApp {

public:
    static const int NUM_VISUALS = 7;
    static const int MAX_NOTES = 10;
    
	void setup();
    void setupSoundData(int numInsts);
    void setupVisuals(int numVisuals);
	void update();
    void parseIncomingMessages();
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
    
    vector<SoundData> soundData;
    vector<Visual> visuals;
};
