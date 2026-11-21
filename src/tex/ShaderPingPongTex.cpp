#include "ShaderPingPongTex.h"
#include "Layer.h"
#include <cmath>
#ifdef VOSC_RECOVERY_TESTING
#include <deque>
#endif

namespace {
uint64_t feedbackContextGeneration = 1;
std::set<ShaderPingPongTex*> feedbackInstances;
bool validFeedbackSource(const std::string& value) {
    if (value.empty() || value.size() > 128 * 1024 || value.find('\0') != std::string::npos) return false;
    // Same strict UTF-8 decoder used by native recipe strings; bounded fragments
    // also reject overlong encodings, surrogates and out-of-range code points.
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    for (size_t index = 0; index < value.size();) {
        const unsigned char first = bytes[index++];
        if (first <= 0x7f) continue;
        int continuation = 0;
        uint32_t codepoint = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; codepoint = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; codepoint = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; codepoint = first & 0x07; }
        else return false;
        if (index + continuation > value.size()) return false;
        for (int offset = 0; offset < continuation; ++offset) {
            const unsigned char next = bytes[index++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) return false;
    }
    return true;
}
#ifdef VOSC_RECOVERY_TESTING
bool feedbackTraceEnabled = false;
bool feedbackFailAllocation = false;
bool feedbackFailSeedCopy = false;
bool feedbackIncompleteDestination = false;
bool feedbackInvalidateAfterUpdate = false;
uint64_t feedbackTraceSequence = 0;
uint64_t feedbackTraceDropped = 0;
uint64_t feedbackAllocationPairs = 0;
std::deque<ofJson> feedbackTrace;
void feedbackEvent(const std::string& name, const ofJson& detail = ofJson::object()) {
    if (!feedbackTraceEnabled) return;
    ofJson event = detail;
    event["event"] = name;
    event["sequence"] = ++feedbackTraceSequence;
    event["frame"] = ofGetFrameNum();
    event["generation"] = feedbackContextGeneration;
    if (feedbackTrace.size() == 256) { feedbackTrace.pop_front(); ++feedbackTraceDropped; }
    feedbackTrace.push_back(event);
}
ofJson feedbackGlStorage(ShaderPingPongTex& instance) {
    ofJson buffers = ofJson::array();
    if (!instance.fbo.isAllocated()) return buffers;
    GLint previousDraw = 0, previousRead = 0, previousTexture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    for (auto* buffer : {&instance.fbo.source(), &instance.fbo.dest()}) {
        glBindFramebuffer(GL_FRAMEBUFFER, buffer->getId());
        ofJson attachments = ofJson::array();
        GLint maximum = 0, samples = 0, depth = 0, stencil = 0;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maximum);
        glGetIntegerv(GL_SAMPLES, &samples);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depth);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &stencil);
        for (GLint i = 0; i < maximum; ++i) {
            GLint kind = 0, object = 0;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &kind);
            if (kind == GL_NONE) continue;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
            ofJson attachment{{"index", i}, {"object_type", kind}, {"object", object}};
            if (kind == GL_TEXTURE) {
                glBindTexture(GL_TEXTURE_2D, object);
                GLint width = 0, height = 0, format = 0, minFilter = 0, magFilter = 0, wrapS = 0, wrapT = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
                attachment["width"] = width; attachment["height"] = height; attachment["internalformat"] = format;
                attachment["textureTarget"] = buffer->getTexture().getTextureData().textureTarget;
                attachment["minFilter"] = minFilter; attachment["magFilter"] = magFilter;
                attachment["wrapS"] = wrapS; attachment["wrapT"] = wrapT;
            }
            attachments.push_back(attachment);
        }
        buffers.push_back({{"framebuffer", buffer->getId()}, {"status", glCheckFramebufferStatus(GL_FRAMEBUFFER)},
            {"depth_attachment_type", depth}, {"stencil_attachment_type", stencil}, {"samples", samples}, {"color_attachments", attachments}});
    }
    glBindTexture(GL_TEXTURE_2D, previousTexture);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    return buffers;
}
#endif
}
#ifdef VOSC_RECOVERY_TESTING
ofJson ShaderPingPongTex::recoveryTestConfigure(const ofJson& command) {
    if (command.value("reset_events", false)) {
        feedbackTrace.clear(); feedbackTraceSequence = 0; feedbackTraceDropped = 0; feedbackAllocationPairs = 0;
    }
    feedbackTraceEnabled = command.value("enabled", true);
    feedbackFailAllocation = command.value("fail_next_allocation", false);
    feedbackFailSeedCopy = command.value("fail_next_seed_copy", false);
    feedbackIncompleteDestination = command.value("incomplete_next_destination", false);
    feedbackInvalidateAfterUpdate = command.value("invalidate_after_update", false);
    return {{"enabled", feedbackTraceEnabled}, {"capacity", 256}, {"failure_injection", "test-only; no driver allocation failure is inferred"}};
}
ofJson ShaderPingPongTex::recoveryTestSnapshot() {
    // Observes actual watched programs even when the texture remains static.
    const auto generation = feedbackGeneration();
    ofJson instances = ofJson::array();
    for (auto* instance : feedbackInstances) {
        ofJson uniforms = ofJson::object();
        if (instance->isShaderLoaded()) {
            const GLuint program = instance->getShader().getProgram();
            GLint count = 0, maximum = 0;
            glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
            glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maximum);
            std::vector<char> name(std::max(1, maximum));
            for (GLint i = 0; i < count; ++i) {
                GLsizei length = 0; GLint size = 0; GLenum type = 0;
                glGetActiveUniform(program, i, maximum, &length, &size, &type, name.data());
                const std::string key(name.data(), length);
                ofJson uniform{{"type", type}, {"array_size", size}};
                const int components = type == GL_FLOAT ? 1 : type == GL_FLOAT_VEC2 ? 2 : type == GL_FLOAT_VEC3 ? 3 : type == GL_FLOAT_VEC4 ? 4 : 0;
                if (components && size == 1) {
                    GLfloat values[4] = {};
                    glGetUniformfv(program, glGetUniformLocation(program, key.c_str()), values);
                    uniform["values"] = ofJson::array();
                    for (int component = 0; component < components; ++component) uniform["values"].push_back(values[component]);
                }
                uniforms[key] = uniform;
            }
        }
        instances.push_back({{"path", instance->path}, {"bounded", instance->replayBounded}, {"uniforms", uniforms},
            {"program", instance->isShaderLoaded() ? instance->getShader().getProgram() : 0},
            {"allocated", instance->fbo.isAllocated()}, {"updates", instance->completedUpdates},
            {"successful_generation", instance->lastSuccessfulGeneration}, {"storage", feedbackGlStorage(*instance)}});
    }
    return {{"generation", generation}, {"allocation_pairs", feedbackAllocationPairs}, {"color_allocations", feedbackAllocationPairs*2},
        {"trace_enabled", feedbackTraceEnabled}, {"trace_dropped", feedbackTraceDropped},
        {"events", feedbackTrace}, {"instances", instances}};
}
#endif
ShaderPingPongTex::ShaderPingPongTex(string name, const vector<float>& args, bool tracked)
    : FboPingPongTex(name, args) {
    if (tracked) trackFeedback();
}
void ShaderPingPongTex::trackFeedback() {
    if (feedbackInstances.insert(this).second) invalidateFeedbackGeneration();
}
ShaderPingPongTex::~ShaderPingPongTex() {
    if (feedbackInstances.erase(this)) invalidateFeedbackGeneration();
}
uint64_t ShaderPingPongTex::feedbackGeneration() {
    // Watched shaders may reload while their holder is static, without update().
    for (auto* instance : feedbackInstances) if (instance->isShaderLoaded()) {
        const auto source = instance->getShader().getShaderSource(GL_FRAGMENT_SHADER);
        if (source != instance->observedSource) {
            instance->observedSource = source;
            invalidateFeedbackGeneration();
#ifdef VOSC_RECOVERY_TESTING
            feedbackEvent("watched_source_changed", {{"path", instance->path}, {"program", instance->getShader().getProgram()}});
#endif
        }
    }
    return feedbackContextGeneration;
}
void ShaderPingPongTex::invalidateFeedbackGeneration() { ++feedbackContextGeneration; }

bool ShaderPingPongTex::getExportSource(std::string& source, std::string& reason) const {
    if (!Shader::getExportSource(source, reason)) return false;
    if (!validFeedbackSource(source)) {
        reason = "feedback retained fragment must be nonempty bounded UTF-8 without NUL";
        return false;
    }
    return true;
}

bool ShaderPingPongTex::prepareFeedback() {
    if (isLoaded()) {
        std::string retainedSource, reason;
        if (replayBounded && !getExportSource(retainedSource, reason)) {
            ofLogError() << reason;
            return false;
        }
        const auto source = getShader().getShaderSource(GL_FRAGMENT_SHADER);
        if (source != observedSource) {
            observedSource = source;
            invalidateFeedbackGeneration();
        }
        return true;
    }
    if (replayBounded) {
        const auto paths = Shader::getShaderPaths(path);
        const ofFile file(paths.fragPath);
        if (paths.fragPath.empty() || !file.isFile() || file.getSize() > 128 * 1024 ||
            !validFeedbackSource(ofBufferFromFile(paths.fragPath, true).getText())) {
            ofLogError() << "feedback fragment must be nonempty bounded UTF-8 without NUL";
            return false;
        }
    }
    if (!load(path)) {
        ofLogError() << "feedback shader compile failed: " << path;
        return false;
    }
#ifdef VOSC_RECOVERY_TESTING
    feedbackEvent("compiled", {{"path", path}, {"program", getShader().getProgram()}});
#endif
    std::string source, reason;
    if (replayBounded && !getExportSource(source, reason)) {
        ofLogError() << reason;
        return false;
    }
    prevPath = path;
    observedSource = getShader().getShaderSource(GL_FRAGMENT_SHADER);
    invalidateFeedbackGeneration();
    return true;
}

bool ShaderPingPongTex::validateConfiguration(TexData& data, std::string& reason) const {
    std::string retainedSource;
    if (!getExportSource(retainedSource, reason)) return false;
    const auto size = data.getSize();
    const auto& settings = data.getFboSettings();
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.x > 800 ||
        size.y < 1 || size.y > 600 || std::floor(size.x) != size.x || std::floor(size.y) != size.y ||
        settings.numColorbuffers != 1 || !settings.colorFormats.empty() || settings.useDepth ||
        settings.useStencil || settings.depthStencilAsTexture || settings.numSamples != 0 ||
        settings.textureTarget != GL_TEXTURE_2D || settings.internalformat != GL_RGBA8 ||
        settings.wrapModeHorizontal != GL_CLAMP_TO_EDGE || settings.wrapModeVertical != GL_CLAMP_TO_EDGE ||
        settings.minFilter != GL_LINEAR || settings.maxFilter != GL_LINEAR) {
        reason = "feedback storage is outside RGBA8/2D bounded configuration";
        return false;
    }
    for (const auto& name : {"speed", "timePct"}) {
        const auto found = data.getVars().find(name);
        const auto value = found == data.getVars().end() ? nullptr : std::dynamic_pointer_cast<Variable<float>>(found->second);
        if (!value || !value->isConst() || value->size() != 1) {
            reason = "feedback holder scalar must be a constant"; return false;
        }
    }
    for (const auto& name : {"color", "tint"}) {
        const auto found = data.getVars().find(name);
        const auto value = found == data.getVars().end() ? nullptr : std::dynamic_pointer_cast<Variable<ofFloatColor>>(found->second);
        if (!value || !value->isConst() || value->size() != 1) {
            reason = "feedback color/tint must be constant white"; return false;
        }
    }
    const auto dimension = data.getVars().find("size");
    const auto sizeValue = dimension == data.getVars().end() ? nullptr : std::dynamic_pointer_cast<Variable<glm::vec3>>(dimension->second);
    if (!sizeValue || !sizeValue->isConst() || sizeValue->size() != 1 || sizeValue->get().z != 0) {
        reason = "feedback size must be a constant 2D extent"; return false;
    }
    if (data.noClear || data.blendMode != OF_BLENDMODE_DISABLED || !data.aspectRatio ||
        data.getVar("speed") != 1 || data.getVar("timePct") != 0 ||
        data.getVarColor("color") != ofFloatColor(1, 1, 1, 1) ||
        data.getVarColor("tint") != ofFloatColor(1, 1, 1, 1)) {
        reason = "feedback holder settings are outside replay scope";
        return false;
    }
    if (!args.empty() || !getTextures().empty() || !getBuffers().empty() || !vars.empty()) {
        reason = "feedback nested resources and shader-owned bindings are unsupported"; return false;
    }
    // A promoted direct-source program was built with the fixed vertex stage.
    // Its execution and export no longer depend on the original watched file.
    if (watchedShader()) {
        const auto paths = Shader::getShaderPaths(path);
        if (paths.vertPath != "shaders/passthru.vert" || !paths.geomPath.empty()) {
            reason = "feedback custom vertex/geometry source is unsupported"; return false;
        }
    }
    if (!isShaderLoaded()) { reason = "feedback shader is not compiled"; return false; }
    const auto& types = getUniforms();
    for (const auto& entry : types) {
        if (entry.second.isArray()) {
            reason = "feedback uniform arrays are unsupported"; return false;
        }
    }
    static const std::set<std::string> holderBuiltins = {"size", "speed", "color", "tint", "timePct"};
    static const std::map<std::string, GLenum> shaderBuiltins = {{"time", GL_FLOAT}, {"resolution", GL_FLOAT_VEC2},
        {"random", GL_INT}, {"srctex", GL_SAMPLER_2D}, {"projectionMatrix", GL_FLOAT_MAT4},
        {"modelViewMatrix", GL_FLOAT_MAT4}, {"modelViewProjectionMatrix", GL_FLOAT_MAT4}, {"textureMatrix", GL_FLOAT_MAT4}};
    size_t controls = 0;
    for (const auto& item : data.getVars()) {
        if (holderBuiltins.count(item.first)) continue;
        if (++controls > 64 || shaderBuiltins.count(item.first)) {
            reason = "feedback controls exceed bounds or override a built-in"; return false;
        }
        const auto reflected = types.find(item.first);
        int expected = 0;
        if (reflected != types.end()) {
            if (reflected->second.type == GL_FLOAT) expected = 1;
            else if (reflected->second.type == GL_FLOAT_VEC2) expected = 2;
            else if (reflected->second.type == GL_FLOAT_VEC3) expected = 3;
            else if (reflected->second.type == GL_FLOAT_VEC4) expected = 4;
        }
        int actual = 0;
        std::vector<float> components;
        if (auto value = std::dynamic_pointer_cast<Variable<float>>(item.second)) {
            if (value->isConst()) { components = value->getVec(); actual = components.size(); }
        } else if (auto value = std::dynamic_pointer_cast<Variable<glm::vec2>>(item.second)) {
            if (value->isConst() && value->size() == 1) { actual = 2; components = {value->get().x, value->get().y}; }
        } else if (auto value = std::dynamic_pointer_cast<Variable<glm::vec3>>(item.second)) {
            if (value->isConst() && value->size() == 1) { actual = 3; components = {value->get().x, value->get().y, value->get().z}; }
        } else if (auto value = std::dynamic_pointer_cast<Variable<ofFloatColor>>(item.second)) {
            if (value->isConst() && value->size() == 1) { actual = 4; components = {value->get().r, value->get().g, value->get().b, value->get().a}; }
        }
        for (float component : components) if (!std::isfinite(component) || std::abs(component) > 1000000.f) actual = 0;
        if (!expected || actual != expected) {
            reason = "feedback controls require exact reflected float scalar/vector arity"; return false;
        }
    }
    for (const auto& uniform : types) {
        const auto builtin = shaderBuiltins.find(uniform.first);
        if (builtin != shaderBuiltins.end()) {
            if (uniform.second.type != builtin->second) { reason = "feedback built-in uniform type mismatch"; return false; }
            continue;
        }
        if (!data.getVars().count(uniform.first)) {
            reason = "feedback has unsupported or unbound uniforms"; return false;
        }
    }
    return true;
}

bool ShaderPingPongTex::storageMatches(TexData& data) {
    if (!fbo.isAllocated()) return false;
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    bool valid = true;
    for (auto* buffer : {&fbo.source(), &fbo.dest()}) {
        if (buffer->getNumTextures() != 1) { valid = false; break; }
        const auto& texture = buffer->getTexture().getTextureData();
        if (texture.textureTarget != GL_TEXTURE_2D) { valid = false; break; }
        glBindTexture(GL_TEXTURE_2D, texture.textureID);
        GLint format = 0, width = 0, height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        valid = valid && format == GL_RGBA8 && width == data.getSize().x && height == data.getSize().y;
    }
    glBindTexture(GL_TEXTURE_2D, previous);
    return valid;
}

std::shared_ptr<ShaderPingPongTex> ShaderPingPongTex::prepareTrial(
    TexData& data, const std::string& source, std::string& code, std::string& reason) {
    PreparedDirectSource prepared;
    if (!validFeedbackSource(source) || !prepareDirectSource(source, prepared)) {
        code = "shader_failed"; reason = "feedback fragment compile/link failed";
        return nullptr;
    }
    // Validate the holder against the new reflection without touching the
    // live holder or program. Built-ins can be optimized out independently.
    auto candidate = std::make_shared<ShaderPingPongTex>(path, args, false);
    candidate->replayBounded = true;
    candidate->trialCandidate = true;
    candidate->installDirectSource(std::move(prepared), {});
    candidate->observedSource = candidate->getShader().getShaderSource(GL_FRAGMENT_SHADER);
    if (!candidate->validateConfiguration(data, reason)) {
        code = "invalid_uniform";
        return nullptr;
    }
    code = "trial_feedback_allocation";
    reason = "candidate feedback storage could not be allocated";
#ifdef VOSC_RECOVERY_TESTING
    if (feedbackFailAllocation) {
        feedbackFailAllocation = false;
        feedbackEvent("injected_trial_allocation_denial");
        return nullptr;
    }
#endif
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
    candidate->FboPingPongTex::update(data);
    if (glGetError() != GL_NO_ERROR || !candidate->storageMatches(data)) return nullptr;

    GLint oldRead = 0, oldDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.source().getId());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, candidate->fbo.dest().getId());
    bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, candidate->fbo.source().getId());
    complete = complete && glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        const auto size = data.getSize();
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, size.x, size.y, 0, 0, size.x, size.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
    const auto copyError = glGetError();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, oldRead);
    if (!complete) return nullptr;
    code = "trial_feedback_seed_copy";
    reason = "could not seed candidate from baseline read attachment";
#ifdef VOSC_RECOVERY_TESTING
    if (feedbackFailSeedCopy) {
        feedbackFailSeedCopy = false;
        feedbackEvent("injected_trial_seed_copy_failure");
        return nullptr;
    }
#endif
    if (copyError != GL_NO_ERROR) return nullptr;
#ifdef VOSC_RECOVERY_TESTING
    feedbackEvent("trial_seeded", {{"baseline_updates", completedUpdates},
        {"baseline_program", getShader().getProgram()}, {"baseline_source_fbo", fbo.source().getId()},
        {"baseline_destination_fbo", fbo.dest().getId()}, {"candidate_program", candidate->getShader().getProgram()},
        {"candidate_source_fbo", candidate->fbo.source().getId()}, {"candidate_destination_fbo", candidate->fbo.dest().getId()}});
#endif
    return candidate;
}

bool ShaderPingPongTex::pauseTrialWatch() {
    auto* watcher = watchedShader();
    if (!watcher) return true;
    const auto paths = Shader::getShaderPaths(path);
    for (const auto& item : {paths.vertPath, paths.fragPath}) {
        auto file = ofToDataPath(item, true);
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(file, error);
        if (error) { trialWatch.clear(); return false; }
        trialWatch.emplace_back(std::move(file), modified);
    }
    const ofFile fragment(paths.fragPath);
    if (!fragment.isFile() || fragment.getSize() > 128 * 1024 ||
        ofBufferFromFile(paths.fragPath, true).getText() != getShader().getShaderSource(GL_FRAGMENT_SHADER) ||
        trialWatchChanged()) {
        trialWatch.clear();
        return false;
    }
    watcher->disableWatchFiles();
    return true;
}

void ShaderPingPongTex::resumeTrialWatch() {
    if (trialWatch.empty()) return;
    trialWatch.clear();
    if (isShaderLoaded()) {
        if (auto* watcher = watchedShader())
            watcher->enableWatchFiles();
    }
}

bool ShaderPingPongTex::trialWatchChanged() const {
    for (const auto& entry : trialWatch) {
        std::error_code error;
        if (std::filesystem::last_write_time(entry.first, error) != entry.second || error) return true;
    }
    return false;
}

void ShaderPingPongTex::update(TexData& data) {
    // Source compilation must precede allocation, including the ordinary OSC path.
    if (!prepareFeedback()) return;
    std::string reason;
    if (replayBounded && !validateConfiguration(data, reason)) {
        ofLogError() << reason; return;
    }
    const bool allocated = fbo.isAllocated();
#ifdef VOSC_RECOVERY_TESTING
    const bool needsAllocation = !allocated || fbo.source().getWidth() != data.getSize().x || fbo.source().getHeight() != data.getSize().y;
    if (needsAllocation) {
        feedbackEvent("allocation_requested", {{"path", path}, {"width", data.getSize().x}, {"height", data.getSize().y}});
        if (feedbackFailAllocation) {
            feedbackFailAllocation = false;
            feedbackEvent("injected_allocation_denial", {{"simulated", true}});
            ofLogError() << "feedback test-only injected preallocation denial";
            return;
        }
    }
#endif
    FboPingPongTex::update(data);
#ifdef VOSC_RECOVERY_TESTING
    if (needsAllocation && fbo.isAllocated()) {
        ++feedbackAllocationPairs;
        feedbackEvent("allocated", {{"storage", feedbackGlStorage(*this)}});
        if (feedbackTraceEnabled) {
            ofJson white = ofJson::array();
            for (auto* buffer : {&fbo.source(), &fbo.dest()}) {
                ofPixels pixels;
                buffer->readToPixels(pixels);
                uint64_t nonwhite = 0;
                for (size_t i = 0; i < pixels.size(); ++i) if (pixels[i] != 255) ++nonwhite;
                white.push_back({{"bytes", pixels.size()}, {"nonwhite_bytes", nonwhite}});
            }
            feedbackEvent("white_seed_verified", {{"buffers", white}});
        }
    }
#endif
    if (!allocated && fbo.isAllocated()) invalidateFeedbackGeneration();
    if (!fbo.isAllocated()) {
        ofLogError() << "feedback framebuffer allocation failed";
        return;
    }
    if (replayBounded && !storageMatches(data)) {
        ofLogError() << "feedback actual GL storage disagrees with bounded settings";
        return;
    }
    if (trialCandidate) {
        for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
    }
    ofEnableBlendMode(data.blendMode);
    
    fbo.dest().begin();
#ifdef VOSC_RECOVERY_TESTING
    if (feedbackIncompleteDestination) {
        feedbackIncompleteDestination = false;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        feedbackEvent("injected_destination_detachment", {{"simulated_trigger", true}, {"actual_status", glCheckFramebufferStatus(GL_FRAMEBUFFER)}});
    }
#endif
    if (replayBounded && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fbo.dest().end();
        ofDisableBlendMode();
        ofLogError() << "feedback destination framebuffer is incomplete";
        return;
    }
    if (data.getFboSettings().numColorbuffers > 1) {
        fbo.dest().activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
    }
    if (!data.noClear) {
        ofClear(0, 0, 0, 0);
    }
    
#ifdef VOSC_RECOVERY_TESTING
    feedbackEvent("update_begin", {{"program", getShader().getProgram()}, {"source_fbo", fbo.source().getId()}, {"destination_fbo", fbo.dest().getId()}});
#endif
    begin(data);
    for (int i=0; i<fbo.source().getNumTextures(); i++) {
        string name = i == 0 ? "srctex" : "srctex" + ofToString(i);
        setUniformTexture(name, fbo.source().getTexture(i), i);
    }
    fbo.source().draw(0,0);
    end();
        
    fbo.dest().end();
    if (trialCandidate && glGetError() != GL_NO_ERROR) {
        ofDisableBlendMode();
        ofLogError() << "candidate feedback write failed";
        return;
    }
    fbo.swap();
    ++completedUpdates;
    lastSuccessfulGeneration = feedbackGeneration();
#ifdef VOSC_RECOVERY_TESTING
    feedbackEvent("update_complete", {{"updates", completedUpdates}, {"program", getShader().getProgram()}});
    if (feedbackInvalidateAfterUpdate) {
        feedbackInvalidateAfterUpdate = false;
        invalidateFeedbackGeneration();
        feedbackEvent("injected_after_update_invalidation", {{"simulated", true}});
    }
#endif
    
    ofDisableBlendMode();
}

void ShaderPingPongTex::draw(const glm::vec2& pos, const glm::vec2& size) {
    if (!fbo.isAllocated()) return;
    lastDrawFrame = ofGetFrameNum();
    lastDrawUpdate = completedUpdates;
#ifdef VOSC_RECOVERY_TESTING
    feedbackEvent("composed_draw", {{"updates", completedUpdates}, {"source_fbo", fbo.source().getId()}, {"width", size.x}, {"height", size.y}});
#endif
    if (replayBounded) {
        // This scoped texture is a 2D layer; preceding geometry must not make
        // equal-depth fragments disappear when the layer order is changed.
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        // FBO texels use GL bottom-left coordinates; composition is top-down.
        ofPushMatrix();
        ofTranslate(pos.x, pos.y + size.y);
        ofScale(1.f, -1.f);
        fbo.source().draw(0, 0, size.x, size.y);
        ofPopMatrix();
        if (depthTest) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    } else FboPingPongTex::draw(pos, size);
}

void ShaderPingPongTex::choose() {
    path = random();
}

void ShaderPingPongTex::reload() {
    invalidateFeedbackGeneration();
    cache.erase(path);
    Shader::unload();
}

void ShaderPingPongTex::clear() {
    invalidateFeedbackGeneration();
    completedUpdates = 0;
    FboPingPongTex::clear();
}
