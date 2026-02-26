#pragma once

#include "../RuntimeContext.hpp"
#include "../osc/CommandParser.hpp"
#include "../osc/CommandRouter.hpp"
#include "InputSystem.hpp"
#include "RenderSystem.hpp"
#include <deque>

class CommandSystem {
public:
    CommandSystem(RuntimeContext& runtime, RenderSystem& renderSystem, InputSystem& inputSystem)
        : runtime(runtime)
        , renderSystem(renderSystem)
        , inputSystem(inputSystem) {}

    void setup();
    void update();

private:
    bool isQueuedCommand(osc::CommandType type) const;
    bool checkOnset() const;
    void parseMessage(const ofxOscMessage& m);
    void processQueue();
    void handleMidi(const osc::Command& command);
    void routeTargetedResource(const osc::Command& command);
    void invalidCommand(const ofxOscMessage& m) const;
    void invalidCommand(const osc::ParseError& error) const;

    RuntimeContext& runtime;
    RenderSystem& renderSystem;
    InputSystem& inputSystem;
    std::deque<osc::Command> messageQueue;
    osc::CommandParser commandParser;
    osc::CommandRouter commandRouter;
    int waitOnset = -1;
    bool forceOnset = false;
};
