#include "CommandRouter.hpp"

namespace osc {

void CommandRouter::registerHandler(CommandType type, const Handler& handler) {
    handlers[type] = handler;
}

bool CommandRouter::canRoute(CommandType type) const {
    return handlers.find(type) != handlers.end();
}

bool CommandRouter::route(const Command& command) const {
    std::map<CommandType, Handler>::const_iterator it = handlers.find(command.type);
    if (it == handlers.end()) {
        return false;
    }
    it->second(command);
    return true;
}

void CommandRouter::clear() {
    handlers.clear();
}

} // namespace osc

