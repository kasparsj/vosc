#pragma once

#include "Command.hpp"
#include <functional>
#include <map>

namespace osc {

class CommandRouter {
public:
    typedef std::function<void(const Command&)> Handler;

    void registerHandler(CommandType type, const Handler& handler);
    bool canRoute(CommandType type) const;
    bool route(const Command& command) const;
    void clear();

private:
    std::map<CommandType, Handler> handlers;
};

} // namespace osc

