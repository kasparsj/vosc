#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "Logger.h"
#include "VOSC.hpp"

#define PORT 33333
#define FRAME_RATE 30

class ofApp : public ofBaseApp {

public:
	void setup();
	void update();
	void draw();
    void drawConsole();
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
    
    VOSC vosc;
    
    bool showConsole = false;
    std::shared_ptr<Logger> console;
    ofxImGui::Gui gui;
};
