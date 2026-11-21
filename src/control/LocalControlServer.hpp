#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace control {

class LocalControlServer {
public:
    using Handler = std::function<std::string(const std::string&)>;

    LocalControlServer();
    ~LocalControlServer();

    LocalControlServer(const LocalControlServer&) = delete;
    LocalControlServer& operator=(const LocalControlServer&) = delete;
    LocalControlServer(LocalControlServer&&) = delete;
    LocalControlServer& operator=(LocalControlServer&&) = delete;

    bool start(const std::string& absolutePath);
    void update(const Handler& handler, double nowSeconds);
    void stop();

    bool isRunning() const;
    std::string getLastError() const;
    std::size_t clientCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace control
