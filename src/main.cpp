#include "ofMain.h"
#include "ofApp.h"
#include "Config.h"
#include "control/OscPort.hpp"
#if defined(TARGET_OSX) && !(defined(APPSTORE) && APPSTORE)
#include "macos/ManagedData.hpp"
#endif
#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#ifdef VOSC_RECOVERY_TESTING
#include "ofAppGLFWWindow.h"
#endif

#define USE_PROGRAMMABLE_RENDERER

#ifdef USE_PROGRAMMABLE_RENDERER
#include "ofGLProgrammableRenderer.h"
#endif

namespace {
constexpr auto usage = "Usage: vosc [--scene <path/to/scene.vosc.json>] [--size WIDTHxHEIGHT] [--fps FPS] [--scene-input name=value] [--draft-data-root <private-directory> [--draft-sample-port 0]]";

struct StartupOptions {
    std::string scenePath;
    std::optional<std::filesystem::path> draftDataRoot;
    bool draftSamples = false;
    scene::Inputs inputs;
    std::optional<std::pair<int, int>> size;
    std::optional<int> fps;
};

int positiveInteger(std::string_view value, int maximum, const char* option) {
    int number = 0;
    if (!value.empty()) {
        const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
            number > 0 && number <= maximum) return number;
    }
    throw std::invalid_argument(std::string(option) + " expects an integer in 1.." + std::to_string(maximum));
}

StartupOptions parseOptions(int argc, char** argv) {
    StartupOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag != "--scene" && flag != "--size" && flag != "--fps" && flag != "--scene-input"
                && flag != "--draft-data-root" && flag != "--draft-sample-port")
            throw std::invalid_argument("unknown option " + std::string(flag));
        if (i + 1 == argc || std::string_view(argv[i + 1]).rfind("--", 0) == 0 || !argv[i + 1][0])
            throw std::invalid_argument("missing value for " + std::string(flag));
        const std::string_view value = argv[++i];
        if (flag == "--scene" && options.scenePath.empty()) {
            options.scenePath = value;
        } else if (flag == "--draft-data-root" && !options.draftDataRoot) {
            options.draftDataRoot.emplace(value);
        } else if (flag == "--draft-sample-port" && !options.draftSamples) {
            if (value != "0") throw std::invalid_argument("--draft-sample-port requires 0 for an ephemeral loopback port");
            options.draftSamples = true;
        } else if (flag == "--scene-input") {
            const auto separator = value.find('=');
            if (separator == 0 || separator == std::string_view::npos || separator == value.size() - 1)
                throw std::invalid_argument("--scene-input expects name=value");
            if (!options.inputs.emplace(value.substr(0, separator), value.substr(separator + 1)).second)
                throw std::invalid_argument("duplicate scene input " + std::string(value.substr(0, separator)));
        } else if (flag == "--size" && !options.size) {
            const auto separator = value.find('x');
            if (separator == std::string_view::npos)
                throw std::invalid_argument("--size expects WIDTHxHEIGHT");
            options.size = {{positiveInteger(value.substr(0, separator), 8192, "--size"),
                             positiveInteger(value.substr(separator + 1), 8192, "--size")}};
        } else if (flag == "--fps" && !options.fps) {
            options.fps = positiveInteger(value, 1000, "--fps");
        } else {
            throw std::invalid_argument("duplicate option " + std::string(flag));
        }
    }
    if (!options.inputs.empty() && options.scenePath.empty())
        throw std::invalid_argument("--scene-input requires --scene");
    if (options.draftSamples && !options.draftDataRoot)
        throw std::invalid_argument("--draft-sample-port requires --draft-data-root");
    return options;
}

void selectDraftDataRoot(const std::filesystem::path& path) {
#if defined(__unix__) || defined(__APPLE__)
    struct stat status {};
    if (!path.is_absolute() || ::lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)
            || status.st_uid != ::geteuid() || (status.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        throw std::invalid_argument("--draft-data-root requires an absolute private directory owned by the current user");
    const char* socket = std::getenv("VOSC_CONTROL_SOCKET");
    if (!socket || !std::filesystem::path(socket).is_absolute())
        throw std::invalid_argument("--draft-data-root requires an absolute VOSC_CONTROL_SOCKET path");
    const auto resolved = std::filesystem::canonical(path);
    ofSetDataPathRoot(resolved);
    std::cout << "VOSC_DRAFT_DATA_ROOT " << resolved.string() << std::endl;
#else
    throw std::invalid_argument("--draft-data-root requires Unix domain socket support");
#endif
}

class StartupDeadline {
public:
    explicit StartupDeadline(const scene::File& file)
        : thread([this, path = file.path.string(), input = file.mediaInput] {
              std::unique_lock lock(mutex);
              if (condition.wait_for(lock, std::chrono::seconds(15), [this] { return finished; })) return;
              std::fprintf(stderr, "vosc: %s: %s%sstartup timed out before composition\n",
                           path.c_str(), input.c_str(), input.empty() ? "" : ": ");
              std::fflush(stderr);
              // A synchronous backend may have blocked the window thread.
              // Exiting the owned CLI process also releases its OS resources.
              std::_Exit(EXIT_FAILURE);
          }) {}

    ~StartupDeadline() { stop(); }

    void stop() {
        {
            std::lock_guard lock(mutex);
            finished = true;
        }
        condition.notify_one();
        if (thread.joinable()) thread.join();
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::thread thread;
};
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage << std::endl;
        return EXIT_SUCCESS;
    }
    StartupOptions options;
    try {
        options = parseOptions(argc, argv);
    } catch (const std::invalid_argument& error) {
        std::cerr << "vosc: " << error.what() << '\n' << usage << std::endl;
        return EXIT_FAILURE;
    }

    std::optional<unsigned int> oscPort;
    std::optional<scene::File> startupScene;
    try {
        if (options.draftDataRoot) {
            selectDraftDataRoot(*options.draftDataRoot);
        } else {
#if defined(TARGET_OSX)
            const auto contents = ofFilePath::getCurrentExePathFS().parent_path().parent_path();
            const auto bundledData = contents / "Resources/data";
#if defined(APPSTORE) && APPSTORE
            ofSetDataPathRoot(bundledData);
#else
            if (vosc::macos::initializeManagedData(argc == 1 && getppid() == 1)) {
                const auto& root = vosc::macos::managedDataRoot();
                ofSetDataPathRoot(root);
                std::cout << "VOSC_DATA_ROOT " << root.string() << std::endl;
            } else {
                // Non-distribution builds may stage data beside the app.
                std::error_code error;
                if (std::filesystem::is_directory(bundledData, error) &&
                    !std::filesystem::is_directory(contents.parent_path().parent_path() / "data", error)) {
                    ofSetDataPathRoot(bundledData);
                }
            }
#endif
#endif
            oscPort = control::oscPortFromEnvironment();
        }
        if (!options.scenePath.empty()) {
            startupScene = scene::load(options.scenePath, options.inputs);
            CommandSystem::validateStartupScene(*startupScene);
            if (options.size) {
                // Keep window creation and startup replay on the same effective size.
                startupScene->width = options.size->first;
                startupScene->height = options.size->second;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "vosc: ";
        if (!options.scenePath.empty()) std::cerr << options.scenePath << ": ";
        std::cerr << error.what() << std::endl;
        return EXIT_FAILURE;
    }
    const auto [width, height] = options.size.value_or(
        startupScene ? std::pair{startupScene->width, startupScene->height} : std::pair{1024, 768});
#ifdef VOSC_RECOVERY_TESTING
    CommandSystem::traceStartupBoundary("window");
#endif

    std::optional<StartupDeadline> deadline;
    if (startupScene && startupScene->version == 2) deadline.emplace(*startupScene);
#ifdef USE_PROGRAMMABLE_RENDERER
    ofGLWindowSettings settings;
    settings.setGLVersion(GL_VERSION_MAJOR, GL_VERSION_MINOR);
#ifdef GL_PROFILE
    settings.setProfile(GL_PROFILE);
#endif
    settings.setSize(width, height);
#ifdef VOSC_RECOVERY_TESTING
    const char* testSamples = std::getenv("VOSC_TEST_WINDOW_SAMPLES");
    if (testSamples && (std::string_view(testSamples) == "0" || std::string_view(testSamples) == "4")) {
        ofGLFWWindowSettings testSettings(settings);
        testSettings.numSamples = testSamples[0] - '0';
        ofCreateWindow(testSettings);
    } else
#endif
    ofCreateWindow(settings);
#else
    ofSetupOpenGL(width, height, OF_WINDOW);
#endif
    
    bool startupFailed = false;
    const int status = ofRunApp(new ofApp(oscPort, options.draftSamples, std::move(startupScene), startupFailed,
                                        options.fps.value_or(60), [&] {
        if (deadline) deadline->stop();
    }));
    if (startupFailed) return EXIT_FAILURE;
    return options.scenePath.empty() ? EXIT_SUCCESS : status;

}
