#include "osc/CommandParser.hpp"
#include "osc/CommandRouter.hpp"
#include "TestSupport.hpp"
#include <iostream>

using namespace osc;

static bool testRouteReturnsFalseWithoutHandler() {
    CommandRouter router;
    Command command;
    command.type = CommandType::INPUT;

    TEST_REQUIRE(!router.route(command));
    return true;
}

static bool testRouteInvokesRegisteredHandler() {
    CommandRouter router;
    bool called = false;

    router.registerHandler(CommandType::INPUT, [&](const Command& command) {
        called = command.type == CommandType::INPUT;
    });

    Command command;
    command.type = CommandType::INPUT;
    TEST_REQUIRE(router.route(command));
    TEST_REQUIRE(called);
    return true;
}

static bool testRegisterHandlerOverridesPreviousHandler() {
    CommandRouter router;
    int marker = 0;

    router.registerHandler(CommandType::MIDI, [&](const Command&) {
        marker = 1;
    });
    router.registerHandler(CommandType::MIDI, [&](const Command&) {
        marker = 2;
    });

    Command command;
    command.type = CommandType::MIDI;
    TEST_REQUIRE(router.route(command));
    TEST_REQUIRE_EQ(marker, 2);
    return true;
}

static bool testClearRemovesHandlers() {
    CommandRouter router;
    router.registerHandler(CommandType::ONSET_FORCE, [&](const Command&) {});
    TEST_REQUIRE(router.canRoute(CommandType::ONSET_FORCE));

    router.clear();

    TEST_REQUIRE(!router.canRoute(CommandType::ONSET_FORCE));
    Command command;
    command.type = CommandType::ONSET_FORCE;
    TEST_REQUIRE(!router.route(command));
    return true;
}

static bool testParserAndRouterDispatchLayers() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers");
    m.addIntArg(6);
    m.addStringArg("stack");
    auto parsed = parser.parse(m);

    TEST_REQUIRE(parsed.isOk());
    TEST_REQUIRE_EQ(parsed.value().type, CommandType::LAYERS);

    int observedLayerCount = -1;
    Layout observedLayout = Layout::GRID;

    CommandRouter router;
    router.registerHandler(CommandType::LAYERS, [&](const Command& command) {
        observedLayerCount = command.layers.layerCount;
        observedLayout = command.layers.layout;
    });

    TEST_REQUIRE(router.route(parsed.value()));
    TEST_REQUIRE_EQ(observedLayerCount, 6);
    TEST_REQUIRE_EQ(observedLayout, Layout::STACK);
    return true;
}

static bool testParserRejectsLayerPrefixLookalike() {
    CommandParser parser;
    ofxOscMessage m = msg("/layersplus");

    auto parsed = parser.parse(m);
    TEST_REQUIRE(!parsed.isOk());
    TEST_REQUIRE_EQ(parsed.error().code, ParseErrorCode::UNKNOWN_COMMAND);
    return true;
}

int main() {
    int failed = 0;
    int passed = 0;

#define RUN_TEST(fn)                                            \
    do {                                                        \
        if (fn()) {                                             \
            ++passed;                                           \
        }                                                       \
        else {                                                  \
            ++failed;                                           \
            std::cerr << "FAILED: " << #fn << std::endl;       \
        }                                                       \
    } while (0)

    RUN_TEST(testRouteReturnsFalseWithoutHandler);
    RUN_TEST(testRouteInvokesRegisteredHandler);
    RUN_TEST(testRegisterHandlerOverridesPreviousHandler);
    RUN_TEST(testClearRemovesHandlers);
    RUN_TEST(testParserAndRouterDispatchLayers);
    RUN_TEST(testParserRejectsLayerPrefixLookalike);

#undef RUN_TEST

    std::cout << "CommandDispatchTests: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
