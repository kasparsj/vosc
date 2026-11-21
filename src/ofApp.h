#pragma once

#include "ofMain.h"
#include "Config.h"
#if USE_OFX_IMGUI
#include "ofxImGui.h"
#endif
#include "Logger.h"
#include "VOSC.hpp"

#define PORT 33333
#define FRAME_RATE 60

class ofApp : public ofBaseApp {

public:
    ofApp(unsigned int oscPort, std::optional<scene::File> startupScene, bool& startupFailed)
        : oscPort(oscPort), startupScene(std::move(startupScene)), startupFailed(startupFailed) {}

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
    
private:
    void failStartup(const std::string& detail);
    unsigned int oscPort;
    std::optional<scene::File> startupScene;
    bool& startupFailed;

    bool showConsole = false;
    std::shared_ptr<Logger> console;
#if USE_OFX_IMGUI
    ofxImGui::Gui gui;
#endif
};
