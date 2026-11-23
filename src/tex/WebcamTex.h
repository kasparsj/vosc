#pragma once

#include "BaseTex.h"

#ifdef TARGET_OSX
#include <chrono>
#include <optional>
class NativeCameraCapture;
#endif

class WebcamTex : public BaseTex {
public:
    static bool exists(int device);
    static int random();
    
    WebcamTex(string path, const vector<float>& args);
    ~WebcamTex() override;
    void update(TexData& data) override;
    bool startupFrameReady() const override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override;
    bool isFrameNew() override;
    ofTexture& getTexture() override;
    const ofTexture& getTexture() const override;
    void setUseTexture(bool enabled) override;
    bool isUsingTexture() const override;
    ofPixels& getPixels() override;
#ifdef TARGET_OSX
    void setStartupCapture(bool enabled) { startupCapture = enabled; }
#endif

private:
    bool observedFrame = false;
#ifdef TARGET_OSX
    std::shared_ptr<NativeCameraCapture> capture;
    std::optional<std::chrono::steady_clock::time_point> retryAfter;
    ofTexture texture;
    bool frameNew = false;
    bool useTexture = true;
    bool startupCapture = false;
#else
    ofVideoGrabber vidGrabber;
#endif
};
