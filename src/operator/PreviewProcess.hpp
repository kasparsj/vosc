#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace vosc::operator_ui {

class PreviewProcess {
public:
    enum class State { Idle, Starting, Ready, Stopping, Failed };

    PreviewProcess();
    ~PreviewProcess();

    static bool supported();
    State state() const;
    const std::string& title() const;
    const std::string& diagnostic() const;
    unsigned int port() const;
    void start(const std::filesystem::path& manifest, const std::string& title,
               const std::map<std::string, std::string>& inputs = {});
    void tick();
    void focus();
    void stop();
    void shutdown();

private:
    struct Process;
    std::unique_ptr<Process> process;
};

// The launcher's inherited descriptors keep child-mode startup private to an owner.
class PreviewChild {
public:
    PreviewChild();
    ~PreviewChild();
    PreviewChild(const PreviewChild&) = delete;
    PreviewChild& operator=(const PreviewChild&) = delete;

    bool poll() const;
    bool announceReady(const std::string& marker) const;

private:
    std::filesystem::path directory;
};

} // namespace vosc::operator_ui
#endif
