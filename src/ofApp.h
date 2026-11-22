#pragma once

#include "ofMain.h"
#include "Config.h"
#if VOSC_OPERATOR_UI
#include "operator/DiagnosticOverlay.hpp"
#endif
#include "Logger.h"
#include "VOSC.hpp"

class ofApp : public ofBaseApp {

public:
    ofApp(unsigned int oscPort, std::optional<scene::File> startupScene, bool& startupFailed,
          int frameRate, std::function<void()> finishStartup)
        : oscPort(oscPort), startupScene(std::move(startupScene)), startupFailed(startupFailed),
          frameRate(frameRate), finishStartup(std::move(finishStartup)) {}

	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
#if VOSC_OPERATOR_UI
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
#endif
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);
    
    VOSC vosc;
    
private:
    void failStartup(const std::string& detail);
    unsigned int oscPort;
    std::optional<scene::File> startupScene;
    bool& startupFailed;
    int frameRate;
    std::function<void()> finishStartup;

#if VOSC_OPERATOR_UI
    std::shared_ptr<Logger> logger;
    DiagnosticOverlay overlay;
#endif
};
