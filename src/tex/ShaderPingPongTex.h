#pragma once

#include "../shader/Shader.h"
#include "FboPingPongTex.h"
#include <filesystem>

class ShaderPingPongTex : public Shader, public FboPingPongTex {
    
public:
    ShaderPingPongTex(string name, const vector<float>& args, bool tracked = true);
    ~ShaderPingPongTex() override;
    
    static uint64_t feedbackGeneration();
    static void invalidateFeedbackGeneration();
#ifdef VOSC_RECOVERY_TESTING
    static ofJson recoveryTestSnapshot();
    static ofJson recoveryTestConfigure(const ofJson& command);
#endif
    bool replayBounded = false;
    bool trialCandidate = false;
    bool prepareFeedback();
    bool getExportSource(std::string& source, std::string& reason) const;
    static bool validateUniforms(const Uniforms& uniforms, const std::map<std::string, int>& controls,
                                 std::string& reason);
    bool validateConfiguration(TexData& data, std::string& reason) const;
    std::shared_ptr<ShaderPingPongTex> prepareTrial(TexData& data, const std::string& source,
                                                   std::string& code, std::string& reason);
    void trackFeedback();
    bool pauseTrialWatch();
    void resumeTrialWatch();
    bool trialWatchChanged() const;
    bool storageMatches(TexData& data);
    bool drewCurrentUpdate() const {
        return lastDrawFrame == ofGetFrameNum() && lastDrawUpdate == completedUpdates;
    }
    bool firstUpdateComplete() const { return completedUpdates != 0; }
    uint64_t updateCount() const { return completedUpdates; }
    uint64_t successfulUpdateGeneration() const { return lastSuccessfulGeneration; }
    void update(TexData& data) override;
    void draw(const glm::vec2& pos, const glm::vec2& size) override;
    void choose() override;
    void reload() override;
    void clear() override;
private:
    std::vector<std::pair<std::string, std::filesystem::file_time_type>> trialWatch;
    uint64_t lastDrawFrame = UINT64_MAX;
    uint64_t lastDrawUpdate = 0;
    uint64_t completedUpdates = 0;
    uint64_t lastSuccessfulGeneration = 0;
    std::string observedSource;
};
