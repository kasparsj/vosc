#include "Input.h"
#include "VariablePool.h"
#ifdef TARGET_OSX
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <array>
#include <cstddef>
#include <dispatch/dispatch.h>
#include <set>
#include <optional>
#endif

#ifdef TARGET_OSX
namespace {
dispatch_queue_t audioLifecycleQueue() {
    static dispatch_queue_t queue = dispatch_queue_create("org.vosc.audio.lifecycle", DISPATCH_QUEUE_SERIAL);
    return queue;
}

std::atomic<unsigned> audioSlots{0};
constexpr unsigned maxAudioSlots = 16;
struct AudioDevice {
    AudioDeviceID id;
    std::string name;
    std::string manufacturer;
    unsigned channels;
};

std::string deviceString(AudioDeviceID id, AudioObjectPropertySelector selector) {
    const AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &value) != noErr || !value) return {};
    std::array<char, 1024> text{};
    const bool read = CFStringGetCString(value, text.data(), text.size(), kCFStringEncodingUTF8);
    CFRelease(value);
    return read ? text.data() : "";
}

std::vector<AudioDevice> inputDevices() {
    const AudioObjectPropertyAddress devices{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devices, 0, nullptr, &size) != noErr)
        throw std::runtime_error("could not enumerate CoreAudio devices");
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (ids.empty()) return {};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devices, 0, nullptr, &size, ids.data()) != noErr)
        throw std::runtime_error("could not read CoreAudio devices");
    ids.resize(size / sizeof(AudioDeviceID));

    const AudioObjectPropertyAddress configuration{kAudioDevicePropertyStreamConfiguration,
                                                   kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain};
    std::vector<AudioDevice> inputs;
    for (const auto id : ids) {
        if (AudioObjectGetPropertyDataSize(id, &configuration, 0, nullptr, &size) != noErr ||
            size < offsetof(AudioBufferList, mBuffers)) continue;
        auto memory = std::make_unique<std::byte[]>(size);
        auto* buffers = reinterpret_cast<AudioBufferList*>(memory.get());
        if (AudioObjectGetPropertyData(id, &configuration, 0, nullptr, &size, buffers) != noErr) continue;
        unsigned channels = 0;
        for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index)
            channels += buffers->mBuffers[index].mNumberChannels;
        if (channels) inputs.push_back({id, deviceString(id, kAudioObjectPropertyName),
            deviceString(id, kAudioObjectPropertyManufacturer), channels});
    }
    return inputs;
}

void checkAudioStatus(OSStatus status, const char* operation) {
    if (status != noErr) throw std::runtime_error(std::string(operation) + " (CoreAudio " + std::to_string(status) + ")");
}

std::set<AudioDeviceID>& reservedAudioDevices() {
    // Accessed on the lifecycle queue only.
    static auto* devices = new std::set<AudioDeviceID>();
    return *devices;
}

void queueAudioWork(std::function<void()> work) {
    dispatch_async_f(audioLifecycleQueue(), new std::function<void()>(std::move(work)), [](void* task) {
        std::unique_ptr<std::function<void()>> run(static_cast<std::function<void()>*>(task));
        (*run)();
    });
}
}

class AudioInputCapture : public std::enable_shared_from_this<AudioInputCapture> {
public:
    static std::shared_ptr<AudioInputCapture> start(std::string device, unsigned channels, unsigned bufferSize) {
        auto capture = std::make_shared<AudioInputCapture>();
        if (audioSlots.fetch_add(1, std::memory_order_acq_rel) >= maxAudioSlots) {
            --audioSlots;
            capture->fail("The audio backend is still retiring a previous request. Try again.");
            return capture;
        }
        capture->hasSlot.store(true, std::memory_order_release);
        queueAudioWork([capture, device = std::move(device), channels, bufferSize] {
            if (!capture->retired.load(std::memory_order_acquire)) capture->open(device, channels, bufferSize);
        });
        return capture;
    }

    static void retire(std::shared_ptr<AudioInputCapture> capture) {
        if (!capture || capture->retired.exchange(true, std::memory_order_acq_rel) ||
            !capture->hasSlot.load(std::memory_order_acquire)) return;
        queueAudioWork([capture] { capture->close(); });
    }

    bool ready() const { return openCompleted.load(std::memory_order_acquire) && sequence() && error().empty(); }
    uint64_t sequence() const { return samples->sequence.load(std::memory_order_acquire); }
    float amplitude() const { return samples->amplitude.load(std::memory_order_relaxed); }
    float smooth() const { return samples->smooth.load(std::memory_order_relaxed); }

    std::string error() const {
        // The single lifecycle writer publishes the immutable opening error once.
        if (openCompleted.load(std::memory_order_acquire) && !openError.empty()) return openError;
        if (const auto status = callbackError.load(std::memory_order_relaxed); status != noErr)
            return "Audio capture stopped producing samples (CoreAudio " + std::to_string(status) + ").";
        return {};
    }

private:
    struct Samples {
        std::atomic<uint64_t> sequence{0};
        std::atomic<float> amplitude{0.f};
        std::atomic<float> smooth{0.f};

        void publish(float rms) {
            amplitude.store(rms, std::memory_order_relaxed);
            smooth.store(0.93f * smooth.load(std::memory_order_relaxed) + 0.07f * rms, std::memory_order_relaxed);
            sequence.fetch_add(1, std::memory_order_release);
        }
    };

    static OSStatus receive(void* owner, AudioUnitRenderActionFlags* flags,
                            const AudioTimeStamp* time, UInt32, UInt32 frames, AudioBufferList*) {
        auto& capture = *static_cast<AudioInputCapture*>(owner);
        if (capture.retired.load(std::memory_order_relaxed)) return noErr;
        const auto count = std::size_t(frames) * capture.channels;
        if (count > capture.callbackBuffer.size()) {
            capture.callbackError.store(kAudioUnitErr_TooManyFramesToProcess, std::memory_order_relaxed);
            return noErr;
        }
        AudioBufferList buffers{1, {{capture.channels, UInt32(count * sizeof(float)), capture.callbackBuffer.data()}}};
        const auto status = AudioUnitRender(capture.unit, flags, time, 1, frames, &buffers);
        if (status != noErr) {
            capture.callbackError.store(status, std::memory_order_relaxed);
            return noErr;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const auto sample = capture.callbackBuffer[index];
            capture.energy += double(sample) * sample;
            if (++capture.windowSamples == capture.samplesPerWindow) {
                capture.samples->publish(std::sqrt(capture.energy / capture.samplesPerWindow) / capture.channels);
                capture.energy = 0;
                capture.windowSamples = 0;
            }
        }
        return noErr;
    }

    void open(const std::string& device, unsigned channelCount, unsigned bufferSize) {
        try {
            const auto devices = inputDevices();
            const auto selected = std::find_if(devices.begin(), devices.end(), [&](const AudioDevice& candidate) {
                return candidate.channels >= channelCount &&
                    (candidate.name.find(device) != std::string::npos ||
                     (candidate.manufacturer + ": " + candidate.name).find(device) != std::string::npos);
            });
            if (selected == devices.end()) {
                fail("Audio input " + device + " is unavailable for " + std::to_string(channelCount) + " channels.");
                return;
            }
            if (!reservedAudioDevices().insert(selected->id).second) {
                fail("Audio input " + selected->name + " is held by the current scene or still closing. Release it before retrying.");
                return;
            }
            reservation = selected->id;
            openDevice(*selected, channelCount, bufferSize);
        } catch (const std::exception& error) {
            fail("Could not open audio input " + device + ": " + error.what());
        }
    }

    void openDevice(const AudioDevice& device, unsigned channelCount, unsigned bufferSize) {
        // AUHAL takes input through element 1. Keep the device's current sample
        // clock and buffer configuration and apply the selected RMS window locally.
        AudioComponentDescription description{kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
                                               kAudioUnitManufacturer_Apple, 0, 0};
        auto component = AudioComponentFindNext(nullptr, &description);
        if (!component) throw std::runtime_error("CoreAudio input component is unavailable");
        checkAudioStatus(AudioComponentInstanceNew(component, &unit), "create input component");
        const UInt32 enabled = 1, disabled = 0;
        checkAudioStatus(AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input,
                                             1, &enabled, sizeof(enabled)), "enable audio input");
        checkAudioStatus(AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output,
                                             0, &disabled, sizeof(disabled)), "disable audio output");
        checkAudioStatus(AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global,
                                             0, &device.id, sizeof(device.id)), "select audio input device");

        AudioStreamBasicDescription hardware{};
        UInt32 length = sizeof(hardware);
        checkAudioStatus(AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                                             1, &hardware, &length), "read audio input format");
        channels = channelCount;
        const UInt32 sampleFrameBytes = channels * sizeof(float);
        AudioStreamBasicDescription format{hardware.mSampleRate, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked,
                                            sampleFrameBytes, 1, sampleFrameBytes, channels, 32, 0};
        checkAudioStatus(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                             1, &format, sizeof(format)), "set audio input sample format");
        UInt32 callbackFrames = 0;
        length = sizeof(callbackFrames);
        checkAudioStatus(AudioUnitGetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
                                             0, &callbackFrames, &length), "read maximum audio input frame size");
        if (!callbackFrames || callbackFrames > 65536 || channels > 32)
            throw std::runtime_error("audio input frame size exceeds the supported capture budget");
        callbackBuffer.resize(std::size_t(std::max(callbackFrames, 8192u)) * channels);
        if (hardware.mSampleRate <= 0 || hardware.mSampleRate > 384000)
            throw std::runtime_error("audio input reports an unsupported sample rate");
        const auto originalWindow = ofNextPow2(std::clamp(bufferSize, 64u, 8192u));
        const auto framesPerWindow = std::max(1L, std::lround(originalWindow * hardware.mSampleRate / 44100));
        samplesPerWindow = std::size_t(framesPerWindow) * channels;
        const AURenderCallbackStruct callback{receive, this};
        checkAudioStatus(AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global,
                                             0, &callback, sizeof(callback)), "set audio input callback");
        checkAudioStatus(AudioUnitInitialize(unit), "initialize audio input");
        initialized = true;
        if (retired.load(std::memory_order_acquire)) return;
        checkAudioStatus(AudioOutputUnitStart(unit), "start audio input");
        running = true;
        openCompleted.store(true, std::memory_order_release);
    }

    void fail(std::string detail) {
        openError = std::move(detail);
        openCompleted.store(true, std::memory_order_release);
        close();
    }

    void close() {
        if (unit) {
            if (running) AudioOutputUnitStop(unit);
            if (initialized) AudioUnitUninitialize(unit);
            if (const auto status = AudioComponentInstanceDispose(unit); status != noErr) {
                // The native callback context must stay alive if the OS rejects disposal.
                static auto* retainedCaptures = new std::vector<std::shared_ptr<AudioInputCapture>>();
                if (!retained) retainedCaptures->push_back(shared_from_this());
                retained = true;
                ofLogError("Audio") << "Could not retire audio input (CoreAudio " << status << ").";
                return;
            }
            unit = nullptr;
        }
        if (reservation) {
            reservedAudioDevices().erase(*reservation);
            reservation.reset();
        }
        if (hasSlot.exchange(false, std::memory_order_acq_rel)) --audioSlots;
    }

    std::shared_ptr<Samples> samples = std::make_shared<Samples>();
    AudioUnit unit = nullptr;
    std::optional<AudioDeviceID> reservation;
    std::vector<float> callbackBuffer;
    UInt32 channels = 0;
    std::size_t samplesPerWindow = 0;
    std::size_t windowSamples = 0;
    double energy = 0;
    bool initialized = false;
    bool running = false;
    bool retained = false;
    std::atomic<OSStatus> callbackError{noErr};
    std::string openError;
    std::atomic<bool> openCompleted{false};
    std::atomic<bool> retired{false};
    std::atomic<bool> hasSlot{false};
};
#endif

Input::~Input() {
#ifdef TARGET_OSX
    AudioInputCapture::retire(std::move(audioCapture));
#else
    soundStream.close();
#endif
    vars.clear();
    VariablePool::cleanup(getId());
}

void Input::update() {
#ifdef TARGET_OSX
    if (audioCapture) {
        const auto latest = audioCapture->sequence();
        if (latest != audioSequence) {
            audioSequence = latest;
            amplitude = audioCapture->amplitude();
            ampSmooth = audioCapture->smooth();
            ampScaled = ofMap(ampSmooth, 0.0, getVar("maxAmp"), 0.0, 1.0, true);
        }
        if (!reportedAudioError) {
            if (const auto error = audioCapture->error(); !error.empty()) {
                reportedAudioError = true;
                ofLogError("Audio") << error;
            }
        }
    }
#endif
    ampHist.push_back( ampScaled );
    if( ampHist.size() >= 200 ){
        ampHist.erase(ampHist.begin(), ampHist.begin()+1);
    }
}

void Input::setupStream(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    int numInputChannels = m.getNumArgs() > 2 ? m.getArgAsInt(2) : 1;
    requestedDevice = name;
    requestedChannels = numInputChannels;
    requestedBufferSize = m.getNumArgs() > 3 ? m.getArgAsInt(3) : 256;
#ifdef TARGET_OSX
    AudioInputCapture::retire(std::move(audioCapture));
    audioSequence = 0;
    reportedAudioError = false;
    if (numInputChannels > 0)
        audioCapture = AudioInputCapture::start(name, numInputChannels, requestedBufferSize);
#else
    if (numInputChannels > 0) {
        //instNum = m.getArgAsInt(0);
        auto devices = soundStream.getMatchingDevices(name);
        if (devices.empty()) {
            ofLog() << "found no matching devices for: " << name;
            return;
        }
        ofSoundStreamSettings settings;
        settings.setInDevice(devices[0]);
        settings.setInListener(this);
        settings.sampleRate = 44100;
        settings.numOutputChannels = 0;
        settings.numInputChannels = numInputChannels;
        settings.bufferSize = requestedBufferSize;
        soundStream.setup(settings);
        soundStream.start();
    }
    else {
        soundStream.close();
    }
#endif
}

bool Input::configuredResourceReady() const {
    if (requestedChannels == 0) return true;
#ifdef TARGET_OSX
    return audioCapture && audioCapture->ready();
#else
    return soundStream.getNumInputChannels() == requestedChannels;
#endif
}

bool Input::configuredResourcePending() const {
#ifdef TARGET_OSX
    return audioCapture && !audioCapture->ready() && audioCapture->error().empty();
#else
    return false;
#endif
}

std::string Input::configuredResourceError() const {
#ifdef TARGET_OSX
    return audioCapture ? audioCapture->error() : std::string();
#else
    return {};
#endif
}

#ifndef TARGET_OSX
void Input::audioIn(ofSoundBuffer& input){
    float curVol = 0.0;
    int numCounted = 0;
    int numChan = soundStream.getNumInputChannels();
    for (size_t i = 0; i < input.getNumFrames(); i++){
        for (size_t j=0; j<soundStream.getNumInputChannels(); j++) {
            float sample = input[i * numChan + j] / numChan;
            curVol += sample * sample;
            numCounted++;
        }
    }
    curVol /= (float) numCounted;
    amplitude = sqrt( curVol );
    
    ampSmooth *= 0.93;
    ampSmooth += 0.07 * amplitude;
    ampScaled = ofMap(ampSmooth, 0.0, getVar("maxAmp"), 0.0, 1.0, true);
}

#endif
