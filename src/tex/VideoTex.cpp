#include "VideoTex.h"
#include "Layer.h"
#include <stdexcept>

#if defined(TARGET_OSX) && defined(__OBJC__)
#include "ofAVFoundationPlayer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <sys/stat.h>

namespace {
dispatch_queue_t videoRetirementQueue() {
    static auto queue = dispatch_queue_create("vosc.video.retirement", DISPATCH_QUEUE_SERIAL);
    return queue;
}

struct DecoderSlot {
    __weak AVURLAsset* asset = nil;
    __weak AVAssetTrack* track = nil;
    __weak ofAVFoundationVideoPlayer* player = nil;
};

DecoderSlot* availableDecoderSlot() {
    // One old renderer-owned decoder, one candidate, and one decoder still
    // retiring. A stalled native completion continues to occupy its slot.
    static std::array<DecoderSlot, 3> slots;
    for (auto& slot : slots) {
        if (slot.asset == nil && slot.track == nil && slot.player == nil) return &slot;
    }
    return nullptr;
}

// Cap one packed pixel buffer at the RGBA byte cost of a DCI 4K frame.
constexpr size_t maxVideoPixels = 4096u * 2160u;
constexpr size_t maxVideoFrameBytes = maxVideoPixels * 4u;

void validateVideoDimensions(double width, double height, GLint sideLimit) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1)
        throw std::runtime_error("The video reports invalid dimensions.");
    if (width > sideLimit || height > sideLimit ||
        static_cast<size_t>(std::ceil(width)) > maxVideoFrameBytes / 4u / static_cast<size_t>(std::ceil(height)))
        throw std::runtime_error("The video exceeds the current resolution limit (" +
            std::to_string(sideLimit) + " pixels per side, up to the pixel count of a 4096 x 2160 frame).");
}

class CandidateVideoPlayer final : public ofAVFoundationPlayer {
public:
    explicit CandidateVideoPlayer(GLint sideLimit) : sideLimit(sideLimit) {}

    ~CandidateVideoPlayer() override {
        if (videoPlayer == nil) return;
        ofAVFoundationVideoPlayer* retiring = videoPlayer;
        videoPlayer = nil;
        // close may wait for OF's AVFoundation lock. Keep native cleanup
        // off the render thread while base destruction releases GL state.
        dispatch_async(videoRetirementQueue(), ^{
            [retiring close];
        });
    }

    void update() override {
        ofAVFoundationPlayer::update();
        if (!bFrameNew) return;

        const auto frame = [videoPlayer getCurrentFrame];
        if (frame == nullptr) {
            bFrameNew = false;
            return;
        }
        const auto width = getWidth();
        const auto height = getHeight();
        const auto decodedWidth = CVPixelBufferGetWidth(frame);
        const auto decodedHeight = CVPixelBufferGetHeight(frame);
        validateVideoDimensions(width, height, sideLimit);
        validateVideoDimensions(decodedWidth, decodedHeight, sideLimit);
        if (decodedWidth != static_cast<size_t>(width) || decodedHeight != static_cast<size_t>(height))
            throw std::runtime_error("The decoded video dimensions differ from the player dimensions.");
        // OF 0.12.1 allocates this storage during synchronous load only.
        // ofVideoPlayer reads pixels for GL upload immediately after update.
        if (bResetPixels || !pixels.isAllocated()) {
            pixels.allocate(width, height, getPixelFormat());
            bResetPixels = false;
        }
    }

    bool ready() const { return isReady(); }
    bool failed() const { return [videoPlayer playerItem].status == AVPlayerItemStatusFailed; }

private:
    const GLint sideLimit;
};

NSArray<NSString*>* videoPreflightKeys() {
    return @[@"playable", @"tracks", @"duration"];
}

NSArray<NSString*>* videoTrackKeys() {
    return @[@"naturalSize", @"formatDescriptions"];
}

bool videoMetadataReady(id<AVAsynchronousKeyValueLoading> source, NSArray<NSString*>* keys,
                        const std::string& path) {
    for (NSString* key in keys) {
        const auto status = [source statusOfValueForKey:key error:nil];
        if (status == AVKeyValueStatusLoading || status == AVKeyValueStatusUnknown) return false;
        if (status != AVKeyValueStatusLoaded)
            throw std::runtime_error("Could not read the video. Choose a playable MOV or MP4: " + path);
    }
    return true;
}

void validateVideoTrack(AVAssetTrack* track, GLint sideLimit) {
    const auto size = track.naturalSize;
    validateVideoDimensions(size.width, size.height, sideLimit);
    const auto formats = track.formatDescriptions;
    if (formats.count == 0)
        throw std::runtime_error("The video reports no encoded format.");
    for (id description in formats) {
        const auto format = (__bridge CMVideoFormatDescriptionRef)description;
        const auto encoded = CMVideoFormatDescriptionGetDimensions(format);
        validateVideoDimensions(encoded.width, encoded.height, sideLimit);
    }
}

struct stat videoIdentity(const std::string& path) {
    struct stat result{};
    if (stat(path.c_str(), &result) != 0 || !S_ISREG(result.st_mode))
        throw std::runtime_error("The selected clip is no longer a regular file: " + path);
    return result;
}

bool sameTime(const timespec& left, const timespec& right) {
    return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}
}
#endif

struct VideoTex::InWindowVideo {
#if defined(TARGET_OSX) && defined(__OBJC__)
    ~InWindowVideo() {
        if (asset == nil) return;
        AVURLAsset* pending = asset;
        dispatch_async(videoRetirementQueue(), ^{
            [pending cancelLoading];
        });
    }

    std::chrono::steady_clock::time_point deadline;
    struct stat identity{};
    DecoderSlot* slot = nullptr;
    AVURLAsset* asset = nil;
    AVAssetTrack* track = nil;
    CandidateVideoPlayer* decoder = nullptr;
    bool playbackStarted = false;
#endif
};

VideoTex::VideoTex(string name, const vector<float>& args) : BaseTex(name, args) {}
VideoTex::~VideoTex() = default;

bool VideoTex::supportsInWindowLoad() {
#if defined(TARGET_OSX) && defined(__OBJC__)
    return true;
#else
    return false;
#endif
}

void VideoTex::useInWindowLoad() {
    if (!supportsInWindowLoad())
        throw std::runtime_error("This build has no qualified in-window video loader.");
    inWindow = std::make_unique<InWindowVideo>();
}

void VideoTex::verifyInWindowInput() const {
#if defined(TARGET_OSX) && defined(__OBJC__)
    if (!inWindow) return;
    const auto now = videoIdentity(path);
    const auto& first = inWindow->identity;
    if (first.st_dev != now.st_dev || first.st_ino != now.st_ino || first.st_size != now.st_size ||
        !sameTime(first.st_mtimespec, now.st_mtimespec) || !sameTime(first.st_ctimespec, now.st_ctimespec))
        throw std::runtime_error("The selected clip changed while loading: " + path);
#endif
}

vector<string> loadLocalVideos()
{
    ofDirectory dir("videos");
    vector<string> videos;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "mov") {
            videos.push_back(file.getFileName());
        }
    }
    return videos;
}

string VideoTex::random() {
    static const auto cache = loadLocalVideos();
    if (cache.size() > 0) {
        return cache[int(ofRandom(cache.size()))];
    }
    return "";
}

void VideoTex::update(TexData& data) {
    if (inWindow) {
        updateInWindow(data);
        return;
    }
    if (!videoPlayer.isLoaded()) {
        observedFrame = false;
        startupError.clear();
        videoPlayer.close();
        string absPath = path;
        if (!ofFilePath::isAbsolute(absPath)) {
            absPath = ofToDataPath("videos/" + path);
            if (!ofFile(absPath).exists()) {
                absPath = ofToDataPath(absPath);
            }
        }
        if (videoPlayer.load(absPath)) {
            initializePlayback(data);
            data.setSize(videoPlayer.getWidth(), videoPlayer.getHeight());
        }
        else {
            startupError = "could not load video: " + path;
            ofLogError() << startupError;
            path = prevPath;
            return;
        }
    }
    else if (data.getVar("onset")) {
        seek(data.getVarPercent("timePct"));
    }
    advancePlayback(data);
}

void VideoTex::updateInWindow(TexData& data) {
#if defined(TARGET_OSX) && defined(__OBJC__)
    if (!startupError.empty()) return;
    auto& pending = *inWindow;
    const auto now = std::chrono::steady_clock::now();
    try {
        if (!pending.slot) {
            pending.slot = availableDecoderSlot();
            if (!pending.slot)
                throw std::runtime_error("Video resources are still closing. Try loading the clip again.");
            pending.identity = videoIdentity(path);
            pending.deadline = now + std::chrono::seconds(8);
            NSString* filename = [NSString stringWithUTF8String:path.c_str()];
            if (filename == nil)
                throw std::runtime_error("Could not open the selected video path: " + path);
            pending.asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:filename] options:nil];
            if (pending.asset == nil)
                throw std::runtime_error("Could not open the selected video: " + path);
            pending.slot->asset = pending.asset;
            // Inspect media asynchronously. No callback takes ownership of
            // the scene, texture, or render target.
            [pending.asset loadValuesAsynchronouslyForKeys:videoPreflightKeys() completionHandler:^{}];
            return;
        }

        if (!observedFrame && now >= pending.deadline)
            throw std::runtime_error("The video did not decode a frame in time. Try a local H.264 clip: " + path);

        if (pending.asset != nil) {
            if (pending.track == nil) {
                if (!videoMetadataReady(pending.asset, videoPreflightKeys(), path)) return;
                const auto duration = CMTimeGetSeconds(pending.asset.duration);
                AVAssetTrack* track = [pending.asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
                if (!pending.asset.playable || track == nil || !std::isfinite(duration) || duration <= 0)
                    throw std::runtime_error("The selected clip has no playable video track: " + path);
                pending.track = track;
                pending.slot->track = track;
                [track loadValuesAsynchronouslyForKeys:videoTrackKeys() completionHandler:^{}];
                return;
            }
            if (!videoMetadataReady(pending.track, videoTrackKeys(), path)) return;
            GLint textureLimit = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
            if (textureLimit <= 0)
                throw std::runtime_error("Could not determine the renderer's video texture limit.");
            const auto sideLimit = std::min(textureLimit, 4096);
            validateVideoTrack(pending.track, sideLimit);
            verifyInWindowInput();
            auto decoder = std::make_shared<CandidateVideoPlayer>(sideLimit);
            pending.decoder = decoder.get();
            videoPlayer.setPlayer(std::move(decoder));
            videoPlayer.loadAsync(path);
            pending.slot->player = pending.decoder->getAVFoundationVideoPlayer();
            pending.track = nil;
            pending.asset = nil;
            return;
        }

        if (pending.decoder->failed())
            throw std::runtime_error("The decoder could not open the video. Try a local H.264 clip: " + path);
        if (!pending.decoder->ready()) return;
        if (!pending.playbackStarted) {
            initializePlayback(data);
            if (!data.hasExplicitSize()) data.setSize(videoPlayer.getWidth(), videoPlayer.getHeight());
            pending.playbackStarted = true;
        } else if (data.getVar("onset")) {
            seek(data.getVarPercent("timePct"));
        }
        advancePlayback(data);
    } catch (const std::exception& error) {
        startupError = error.what();
    }
#endif
}

void VideoTex::initializePlayback(TexData& data) {
    videoPlayer.setVolume(0);
    videoPlayer.setLoopState(OF_LOOP_NORMAL);
    seek(data.getVarPercent("timePct"));
    videoPlayer.play();
    prevPath = path;
}

void VideoTex::advancePlayback(TexData& data) {
    aspectRatio = data.aspectRatio;
    videoPlayer.setSpeed(data.getVar("speed"));
    videoPlayer.update();
    if (!observedFrame && videoPlayer.isFrameNew() && videoPlayer.isInitialized() &&
        videoPlayer.getTexture().isAllocated() && videoPlayer.getWidth() > 0 &&
        videoPlayer.getHeight() > 0) {
        observedFrame = true;
        if (!data.hasExplicitSize() && (data.getSize().x <= 0 || data.getSize().y <= 0)) {
            data.setSize(videoPlayer.getWidth(), videoPlayer.getHeight());
        }
    }
}

bool VideoTex::startupFrameReady() const {
    return observedFrame && videoPlayer.isInitialized() && videoPlayer.getTexture().isAllocated();
}

void VideoTex::seek(float pct) {
    videoPlayer.setPosition(pct);
}

void VideoTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (inWindow && !observedFrame) return;
    if (aspectRatio) {
        ofPushMatrix();
        if (videoPlayer.getWidth() > videoPlayer.getHeight()) {
            ofTranslate(0, (size.y - size.x/videoPlayer.getWidth() * videoPlayer.getHeight()) / 2);
            videoPlayer.draw(pos, size.x, size.x/videoPlayer.getWidth() * videoPlayer.getHeight());
        }
        else {
            ofTranslate((size.x - size.y/videoPlayer.getHeight() * videoPlayer.getWidth()) / 2, 0);
            videoPlayer.draw(pos, size.y/videoPlayer.getHeight() * videoPlayer.getWidth(), size.y);
        }
        ofPopMatrix();
    }
    else {
        videoPlayer.draw(pos, size.x, size.y);
    }
}

void VideoTex::choose() {
    path = random();
}
