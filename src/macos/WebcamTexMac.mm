#include "../tex/WebcamTex.h"
#include "../layer/Layer.h"

#import <AVFoundation/AVFoundation.h>
#import <Accelerate/Accelerate.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>

namespace {
constexpr std::size_t maxCapturePixels = 3840u * 2160u;

struct CameraFrames {
    CameraFrames(std::size_t width, std::size_t height) : maxWidth(width), maxHeight(height) {}

    const std::size_t maxWidth;
    const std::size_t maxHeight;
    std::mutex lock;
    ofPixels latest;
    std::string error;
    bool canRetry = false;
    std::atomic<bool> retired{false};
};

std::pair<std::size_t, std::size_t> fitCameraFrame(std::size_t width, std::size_t height,
                                                        const CameraFrames& frames) {
    if (!frames.maxWidth || !frames.maxHeight || (width <= frames.maxWidth && height <= frames.maxHeight))
        return {width, height};
    if (width * frames.maxHeight >= height * frames.maxWidth)
        return {frames.maxWidth, std::max(std::size_t{1}, height * frames.maxWidth / width)};
    return {std::max(std::size_t{1}, width * frames.maxHeight / height), frames.maxHeight};
}

NSArray<AVCaptureDevice*>* cameraDevices() {
    // Match the pinned OF 0.12.1 camera index ordering.
    NSMutableArray<AVCaptureDeviceType>* types =
        [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    if (@available(macOS 14.0, *)) {
        if (&AVCaptureDeviceTypeExternal != nil) [types addObject:AVCaptureDeviceTypeExternal];
        if (&AVCaptureDeviceTypeContinuityCamera != nil) [types addObject:AVCaptureDeviceTypeContinuityCamera];
    }
#endif
    auto* discovery = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
        mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    return [discovery.devices sortedArrayUsingComparator:^NSComparisonResult(AVCaptureDevice* left, AVCaptureDevice* right) {
        const bool leftBuiltIn = [left.localizedName hasPrefix:@"FaceTime"];
        const bool rightBuiltIn = [right.localizedName hasPrefix:@"FaceTime"];
        if (leftBuiltIn != rightBuiltIn) return leftBuiltIn ? NSOrderedAscending : NSOrderedDescending;
        return [left.localizedName compare:right.localizedName];
    }];
}

dispatch_queue_t cameraLifecycleQueue() {
    static dispatch_queue_t queue = dispatch_queue_create("org.vosc.camera.lifecycle", DISPATCH_QUEUE_SERIAL);
    return queue;
}

// Queued start/stop work is bounded even if a native session call stalls.
std::atomic<unsigned> cameraSlots{0};
constexpr unsigned maxCameraSlots = 4;
std::set<std::string>& reservedCameras() {
    // Accessed on the lifecycle queue only.
    static auto* devices = new std::set<std::string>();
    return *devices;
}

std::string message(NSError* error) {
    return error ? std::string(error.localizedDescription.UTF8String ?: "") : std::string();
}
}

@interface VOSCCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    std::shared_ptr<CameraFrames> frames;
}
@end

@implementation VOSCCameraDelegate
- (void)captureOutput:(AVCaptureOutput*)output didOutputSampleBuffer:(CMSampleBufferRef)sample
       fromConnection:(AVCaptureConnection*)connection {
    if (frames->retired.load(std::memory_order_acquire)) return;
    auto* buffer = CMSampleBufferGetImageBuffer(sample);
    if (!buffer || CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA) return;
    const auto width = CVPixelBufferGetWidth(buffer);
    const auto height = CVPixelBufferGetHeight(buffer);
    if (!width || !height || width > maxCapturePixels / height) return;
    if (CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
    ofPixels frame;
    vImage_Error scaleError = kvImageNoError;
    if (auto* pixels = CVPixelBufferGetBaseAddress(buffer)) {
        const auto stride = CVPixelBufferGetBytesPerRow(buffer);
        const auto [fittedWidth, fittedHeight] = fitCameraFrame(width, height, *frames);
        if (fittedWidth == width && fittedHeight == height) {
            frame.setFromAlignedPixels(static_cast<const unsigned char*>(pixels), width, height, OF_PIXELS_BGRA, stride);
        } else {
            // Some macOS cameras return HD despite selecting a smaller format.
            // Scale before publishing so history FBO sizes follow the requested cap.
            frame.allocate(fittedWidth, fittedHeight, OF_PIXELS_BGRA);
            vImage_Buffer source{pixels, height, width, stride};
            vImage_Buffer fitted{frame.getData(), fittedHeight, fittedWidth, fittedWidth * 4};
            scaleError = vImageScale_ARGB8888(&source, &fitted, nullptr, kvImageNoFlags);
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    if (scaleError != kvImageNoError) {
        std::lock_guard lock(frames->lock);
        frames->error = "Could not fit camera frames to the requested dimensions (vImage " + std::to_string(scaleError) + ").";
        return;
    }
    if (!frame.isAllocated()) return;
    std::unique_lock lock(frames->lock, std::try_to_lock);
    if (lock.owns_lock()) frames->latest = std::move(frame);
}
@end

class NativeCameraCapture {
public:
    NativeCameraCapture(int index, std::size_t maxWidth, std::size_t maxHeight)
        : frames(std::make_shared<CameraFrames>(maxWidth, maxHeight)), label("camera " + std::to_string(index)) {}

    static std::shared_ptr<NativeCameraCapture> start(int index, int width, int height, bool capOutputSize) {
        auto capture = std::make_shared<NativeCameraCapture>(index, capOutputSize ? std::max(0, width) : 0,
                                                                  capOutputSize ? std::max(0, height) : 0);
        if (cameraSlots.fetch_add(1, std::memory_order_acq_rel) >= maxCameraSlots) {
            --cameraSlots;
            capture->fail("camera backend is still retiring a previous request; try again", true);
            return capture;
        }
        capture->hasSlot.store(true, std::memory_order_release);
        dispatch_async(cameraLifecycleQueue(), ^{
            @autoreleasepool {
                if (capture->frames->retired.load(std::memory_order_acquire)) return;
                const auto authorization = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
                if (authorization == AVAuthorizationStatusDenied || authorization == AVAuthorizationStatusRestricted) {
                    capture->fail("Camera permission is denied or restricted for " + capture->label, false);
                    return;
                }
                auto* devices = cameraDevices();
                if (index < 0 || static_cast<NSUInteger>(index) >= devices.count) {
                    capture->fail("camera device " + std::to_string(index) +
                                  " is unavailable. Choose a connected camera and retry.", index >= 0);
                    return;
                }
                AVCaptureDevice* device = devices[index];
                capture->label = "camera " + std::to_string(index) + " (" +
                    std::string(device.localizedName.UTF8String ?: "") + ")";
                std::string uid(device.uniqueID.UTF8String ?: "");
                if (uid.empty() || !reservedCameras().insert(uid).second) {
                    capture->fail(capture->label + " is held by the current scene or still closing. Release it before retrying.", true);
                    return;
                }
                capture->reservation = std::move(uid);
                if (authorization == AVAuthorizationStatusAuthorized) {
                    capture->open(device, width, height);
                } else if (authorization == AVAuthorizationStatusNotDetermined) {
                    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                        if (capture->frames->retired.load(std::memory_order_acquire)) return;
                        dispatch_async(cameraLifecycleQueue(), ^{
                            if (capture->frames->retired.load(std::memory_order_acquire)) return;
                            if (granted) capture->open(device, width, height);
                            else capture->fail("Camera permission was denied for " + capture->label, false);
                        });
                    }];
                } else {
                    capture->fail("Camera permission is denied or restricted for " + capture->label, false);
                }
            }
        });
        return capture;
    }

    static void retire(std::shared_ptr<NativeCameraCapture> capture) {
        if (!capture || capture->frames->retired.exchange(true, std::memory_order_acq_rel) ||
            !capture->hasSlot.load(std::memory_order_acquire)) return;
        dispatch_async(cameraLifecycleQueue(), ^{
            @autoreleasepool { capture->close(); }
        });
    }

    bool take(ofPixels& destination, std::string& error, bool& canRetry) {
        std::unique_lock lock(frames->lock, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        error = frames->error;
        canRetry = frames->canRetry;
        if (!frames->latest.isAllocated()) return false;
        std::swap(frames->latest, destination);
        frames->latest.clear();
        return true;
    }

private:
    void fail(std::string detail, bool canRetry) {
        {
            std::lock_guard lock(frames->lock);
            frames->error = std::move(detail);
            frames->canRetry = canRetry;
        }
        close();
    }

    void open(AVCaptureDevice* device, int width, int height) {
        @try {
            NSError* failure = nil;
            auto* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&failure];
            if (!input) {
                const bool denied = [failure.domain isEqualToString:AVFoundationErrorDomain] &&
                                    failure.code == AVErrorApplicationIsNotAuthorizedToUseDevice;
                fail("Could not open " + label + ": " + message(failure), !denied);
                return;
            }
            session = [[AVCaptureSession alloc] init];
            output = [[AVCaptureVideoDataOutput alloc] init];
            if (![session canAddInput:input] || ![session canAddOutput:output]) {
                fail("Could not configure " + label, true);
                return;
            }
            [session beginConfiguration];
            [session addInput:input];
            [session addOutput:output];
            AVCaptureDeviceFormat* chosen = nil;
            double best = std::numeric_limits<double>::infinity();
            for (AVCaptureDeviceFormat* format in device.formats) {
                const auto size = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
                if (size.width <= 0 || size.height <= 0 ||
                    std::size_t(size.width) > maxCapturePixels / size.height) continue;
                const auto distance = std::hypot(double(size.width) - width, double(size.height) - height);
                if (distance < best) {
                    chosen = format;
                    best = distance;
                }
            }
            if (chosen && [device lockForConfiguration:&failure]) {
                @try { device.activeFormat = chosen; }
                @finally { [device unlockForConfiguration]; }
            }

            output.alwaysDiscardsLateVideoFrames = YES;
            output.videoSettings = @{(NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
            frameQueue = dispatch_queue_create("org.vosc.camera.frames", DISPATCH_QUEUE_SERIAL);
            delegate = [[VOSCCameraDelegate alloc] init];
            delegate->frames = frames;
            [output setSampleBufferDelegate:delegate queue:frameQueue];
            [session commitConfiguration];
            if (frames->retired.load(std::memory_order_acquire)) return;
            [session startRunning];
            if (!session.isRunning) fail("Could not start " + label, true);
        } @catch (NSException* exception) {
            fail("Could not start " + label + ": " + std::string(exception.reason.UTF8String ?: ""), true);
        }
    }

    void close() {
        if (output) [output setSampleBufferDelegate:nil queue:nullptr];
        if (frameQueue) dispatch_sync(frameQueue, ^{});
        if (session.isRunning) [session stopRunning];
        delegate = nil;
        output = nil;
        session = nil;
        frameQueue = nullptr;
        if (!reservation.empty()) {
            reservedCameras().erase(reservation);
            reservation.clear();
        }
        if (hasSlot.exchange(false, std::memory_order_acq_rel)) --cameraSlots;
    }

    std::shared_ptr<CameraFrames> frames;
    std::string label;
    std::string reservation;
    std::atomic<bool> hasSlot{false};
    AVCaptureSession* session = nil;
    AVCaptureVideoDataOutput* output = nil;
    VOSCCameraDelegate* delegate = nil;
    dispatch_queue_t frameQueue = nullptr;
};

WebcamTex::WebcamTex(string path, const vector<float>& args) : BaseTex(path, args) {}
WebcamTex::~WebcamTex() { NativeCameraCapture::retire(std::move(capture)); }

bool WebcamTex::exists(int device) {
    @autoreleasepool {
        return device >= 0 && static_cast<NSUInteger>(device) < cameraDevices().count;
    }
}

int WebcamTex::random() {
    @autoreleasepool {
        const auto count = cameraDevices().count;
        return count ? int(ofRandom(count)) : -1;
    }
}

void WebcamTex::update(TexData& data) {
    aspectRatio = data.aspectRatio;
    frameNew = false;
    const auto now = std::chrono::steady_clock::now();
    if (path != prevPath || (!capture && startupError.empty()) || (retryAfter && now >= *retryAfter)) {
        NativeCameraCapture::retire(std::move(capture));
        retryAfter.reset();
        observedFrame = false;
        startupError.clear();
        texture.clear();
        pixels.clear();
        prevPath = path;
        try {
            const int device = std::stoi(path);
            capture = NativeCameraCapture::start(device, args.size() > 0 ? args[0] : 1920,
                                                         args.size() > 1 ? args[1] : 1080,
                                                         args.size() > 1);
        } catch (const std::exception&) {
            startupError = "Invalid camera index: " + path;
        }
    }
    if (!capture || retryAfter) return;
    bool canRetry = false;
    frameNew = capture->take(pixels, startupError, canRetry);
    // Only a running scene can wait for an unavailable or busy device.
    // Startup consumes the first failure and releases the capture promptly.
    if (canRetry && !startupCapture && !startupError.empty())
        retryAfter = now + std::chrono::seconds(1);
    if (!frameNew) return;
    if (useTexture) texture.loadData(pixels);
    if (!observedFrame && !data.hasExplicitSize()) data.setSize(pixels.getWidth(), pixels.getHeight());
    observedFrame = true;
}

bool WebcamTex::startupFrameReady() const { return observedFrame && texture.isAllocated(); }

void WebcamTex::draw(const glm::vec2& pos, const glm::vec2& size) {
    if (!texture.isAllocated()) return;
    auto extent = size;
    if (aspectRatio) {
        const auto width = texture.getWidth();
        const auto height = texture.getHeight();
        extent = width > height ? glm::vec2(size.x, size.x / width * height)
                                : glm::vec2(size.y / height * width, size.y);
    }
    texture.draw(pos.x, pos.y, extent.x, extent.y);
}

void WebcamTex::choose() { path = ofToString(random()); }
bool WebcamTex::isFrameNew() { return frameNew; }
ofTexture& WebcamTex::getTexture() { return texture; }
const ofTexture& WebcamTex::getTexture() const { return texture; }
void WebcamTex::setUseTexture(bool enabled) { useTexture = enabled; }
bool WebcamTex::isUsingTexture() const { return useTexture; }
ofPixels& WebcamTex::getPixels() { return pixels; }
