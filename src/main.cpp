#include "ofMain.h"
#include "ofApp.h"
#include "Config.h"
#include "control/OscPort.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#define USE_PROGRAMMABLE_RENDERER

#ifdef USE_PROGRAMMABLE_RENDERER
#include "ofGLProgrammableRenderer.h"
#endif

namespace {
constexpr auto usage = "Usage: vosc [--scene <path/to/scene.vosc.json>] [--size WIDTHxHEIGHT]";

struct StartupOptions {
    std::string scenePath;
    std::optional<std::pair<int, int>> size;
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
        if (flag != "--scene" && flag != "--size")
            throw std::invalid_argument("unknown option " + std::string(flag));
        if (i + 1 == argc || std::string_view(argv[i + 1]).rfind("--", 0) == 0 || !argv[i + 1][0])
            throw std::invalid_argument("missing value for " + std::string(flag));
        const std::string_view value = argv[++i];
        if (flag == "--scene" && options.scenePath.empty()) {
            options.scenePath = value;
        } else if (flag == "--size" && !options.size) {
            const auto separator = value.find('x');
            if (separator == std::string_view::npos)
                throw std::invalid_argument("--size expects WIDTHxHEIGHT");
            options.size = {{positiveInteger(value.substr(0, separator), 8192, "--size"),
                             positiveInteger(value.substr(separator + 1), 8192, "--size")}};
        } else {
            throw std::invalid_argument("duplicate option " + std::string(flag));
        }
    }
    return options;
}
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

    unsigned int oscPort;
    std::optional<scene::File> startupScene;
    try {
        oscPort = control::oscPortFromEnvironment();
        if (!options.scenePath.empty()) {
            startupScene = scene::load(options.scenePath);
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

#ifdef USE_PROGRAMMABLE_RENDERER
    ofGLWindowSettings settings;
    settings.setGLVersion(GL_VERSION_MAJOR, GL_VERSION_MINOR);
#ifdef GL_PROFILE
    settings.setProfile(GL_PROFILE);
#endif
    settings.setSize(width, height);
    ofCreateWindow(settings);
#else
    ofSetupOpenGL(width, height, OF_WINDOW);
#endif
    
    bool startupFailed = false;
    const int status = ofRunApp(new ofApp(oscPort, std::move(startupScene), startupFailed));
    if (startupFailed) return EXIT_FAILURE;
    return options.scenePath.empty() ? EXIT_SUCCESS : status;

}
