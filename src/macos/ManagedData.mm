#include "ManagedData.hpp"

#import <AppKit/AppKit.h>
#include <CommonCrypto/CommonDigest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <optional>
#include <vector>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace vosc::macos {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using Files = std::map<std::string, std::string>;
using Names = std::set<std::string>;

constexpr size_t MaxMetadata = 16 * 1024 * 1024;
constexpr auto ControlName = ".vosc-managed";
constexpr auto StateName = "state.json";
constexpr auto JournalName = "journal.json";

class Fd {
public:
    Fd() = default;
    explicit Fd(int value) : value(value) {}
    ~Fd() { if (value >= 0) close(value); }
    Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        std::swap(value, other.value);
        return *this;
    }
    explicit operator bool() const { return value >= 0; }
    int get() const { return value; }
    int release() { return std::exchange(value, -1); }
private:
    int value = -1;
};

[[noreturn]] void fail(const fs::path& path, std::string_view message) {
    throw std::runtime_error(path.string() + ": " + std::string(message));
}

[[noreturn]] void systemError(const fs::path& path, int code = errno) {
    fail(path, std::system_category().message(code));
}

struct Directory {
    Fd fd;
    fs::path path;
};

bool sameFile(const struct stat& lhs, const struct stat& rhs) {
    return lhs.st_ino == rhs.st_ino && lhs.st_dev == rhs.st_dev;
}

struct stat info(const Fd& fd, const fs::path& path) {
    struct stat result{};
    if (fstat(fd.get(), &result) < 0) systemError(path);
    return result;
}

std::optional<struct stat> entry(const Directory& parent, const std::string& name) {
    struct stat result{};
    if (fstatat(parent.fd.get(), name.c_str(), &result, AT_SYMLINK_NOFOLLOW) == 0) return result;
    if (errno == ENOENT) return std::nullopt;
    systemError(parent.path / name);
}

bool exactName(const Directory& parent, const std::string& name) {
    Fd fd(openat(parent.fd.get(), ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!fd) systemError(parent.path);
    auto* directory = fdopendir(fd.get());
    if (!directory) systemError(parent.path);
    fd.release();
    const std::unique_ptr<DIR, decltype(&closedir)> owned(directory, closedir);
    while (true) {
        errno = 0;
        const auto* child = readdir(directory);
        if (!child) {
            if (errno) systemError(parent.path);
            return false;
        }
        if (name == child->d_name) return true;
    }
}

void sync(const Fd& fd, const fs::path& path) {
    if (fsync(fd.get()) < 0) systemError(path);
}

void sync(const Directory& directory) {
    sync(directory.fd, directory.path);
}

Directory openDir(const Directory& parent, const std::string& name, bool optional = false) {
    const auto path = parent.path / name;
    const auto before = entry(parent, name);
    if (!before || !S_ISDIR(before->st_mode) || !exactName(parent, name)) {
        if (optional) return {{}, path};
        fail(path, "expected a directory with this exact name");
    }
    Fd fd(openat(parent.fd.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd) systemError(path);
    if (!sameFile(*before, info(fd, path))) fail(path, "directory changed while opening");
    return {std::move(fd), path};
}

Directory createDir(const Directory& parent, const std::string& name) {
    if (mkdirat(parent.fd.get(), name.c_str(), 0700) == 0) sync(parent);
    else if (errno != EEXIST) systemError(parent.path / name);
    return openDir(parent, name);
}

Directory duplicate(const Directory& directory) {
    Fd fd(fcntl(directory.fd.get(), F_DUPFD_CLOEXEC, 0));
    if (!fd) systemError(directory.path);
    return {std::move(fd), directory.path};
}

void requireOwned(const Directory& directory, bool privateDirectory = false) {
    const auto identity = info(directory.fd, directory.path);
    if (identity.st_uid != getuid() || !(identity.st_mode & S_IWUSR)
        || (privateDirectory && (identity.st_mode & (S_IRWXG | S_IRWXO)))) {
        fail(directory.path, privateDirectory ? "expected an owner-only writable directory"
                                              : "expected a directory writable by this account");
    }
}

bool validPath(const std::string& name) {
    if (name.empty() || name.size() >= PATH_MAX || name.find_first_of(":\\") != std::string::npos
        || std::any_of(name.begin(), name.end(), [](unsigned char byte) { return byte < 32 || byte == 127; }))
        return false;
    size_t begin = 0;
    do {
        const auto slash = name.find('/', begin);
        const auto part = std::string_view(name).substr(begin, slash - begin);
        if (part.empty() || part.front() == '.' || part.size() > NAME_MAX) return false;
        if (slash == std::string::npos) return true;
        begin = slash + 1;
    } while (begin <= name.size());
    return false;
}

std::string folded(const std::string& name) {
    NSString* string = [[NSString alloc] initWithBytes:name.data() length:name.size() encoding:NSUTF8StringEncoding];
    if (!string) fail(name, "invalid UTF-8 name");
    static NSLocale* locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
    const char* normalized = [[string stringByFoldingWithOptions:NSCaseInsensitiveSearch locale:locale]
        decomposedStringWithCanonicalMapping].UTF8String;
    if (!normalized) fail(name, "invalid normalized name");
    return normalized;
}

struct Target {
    Directory parent;
    std::string name;
};

Target targetOf(const Directory& root, const std::string& relative, bool createParents = false) {
    const fs::path path(relative);
    auto directory = duplicate(root);
    for (const auto& component : path.parent_path()) {
        directory = createParents ? createDir(directory, component) : openDir(directory, component, true);
        if (!directory.fd) break;
    }
    return {std::move(directory), path.filename()};
}

Fd openFile(const Directory& parent, const std::string& name, bool optional = false) {
    const auto path = parent.path / name;
    const auto before = entry(parent, name);
    if (!before || !S_ISREG(before->st_mode) || before->st_nlink != 1 || !exactName(parent, name)) {
        if (optional) return {};
        fail(path, "expected an ordinary unlinked file with this exact name");
    }
    Fd fd(openat(parent.fd.get(), name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    if (!fd) systemError(path);
    const auto after = info(fd, path);
    if (!sameFile(*before, after) || after.st_nlink != 1) fail(path, "file changed while opening");
    return fd;
}

Fd newFile(const Directory& parent, const std::string& name) {
    Fd fd(openat(parent.fd.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (!fd) systemError(parent.path / name);
    return fd;
}

void writeBytes(const Fd& fd, const fs::path& path, const char* bytes, size_t size) {
    while (size) {
        const auto count = write(fd.get(), bytes, size);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) systemError(path);
        if (count == 0) fail(path, "short write");
        size -= count;
        bytes += count;
    }
}

template<class Read>
void readBytes(const Fd& fd, const fs::path& path, Read consume) {
    std::array<char, 64 * 1024> bytes{};
    while (true) {
        const auto size = read(fd.get(), bytes.data(), bytes.size());
        if (size < 0 && errno == EINTR) continue;
        if (size < 0) systemError(path);
        if (size == 0) return;
        consume(bytes.data(), static_cast<size_t>(size));
    }
}

std::string sha256(const Fd& fd, const fs::path& path, const Fd* copy = nullptr, const fs::path& output = {}) {
    CC_SHA256_CTX hash;
    CC_SHA256_Init(&hash);
    readBytes(fd, path, [&](const char* bytes, size_t size) {
        CC_SHA256_Update(&hash, bytes, static_cast<CC_LONG>(size));
        if (copy) writeBytes(*copy, output, bytes, size);
    });
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> bytes{};
    CC_SHA256_Final(bytes.data(), &hash);
    std::string hex(bytes.size() * 2, '\0');
    constexpr std::string_view alphabet = "0123456789abcdef";
    for (size_t i = 0; i < bytes.size(); ++i) {
        hex[i * 2] = alphabet[bytes[i] >> 4];
        hex[i * 2 + 1] = alphabet[bytes[i] & 0x0f];
    }
    return hex;
}

Json metadata(const Directory& directory, const std::string& name, std::string* hash = nullptr) {
    const auto path = directory.path / name;
    const auto fd = openFile(directory, name);
    std::string bytes;
    readBytes(fd, path, [&](const char* data, size_t size) {
        if (bytes.size() + size > MaxMetadata) fail(path, "metadata exceeds size limit");
        bytes.append(data, size);
    });
    if (hash) {
        if (lseek(fd.get(), 0, SEEK_SET) < 0) systemError(path);
        *hash = sha256(fd, path);
    }
    std::vector<Names> keys;
    auto document = Json::parse(bytes, [&](int, Json::parse_event_t event, Json& item) {
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(item.get<std::string>()).second)
            fail(path, "duplicate metadata key");
        return true;
    }, false);
    if (!document.is_object()) fail(path, "invalid metadata");
    return document;
}

std::string newName(const char* prefix) {
    char random[17]{};
    uint64_t number;
    arc4random_buf(&number, sizeof(number));
    snprintf(random, sizeof(random), "%016llx", static_cast<unsigned long long>(number));
    return std::string(prefix) + random;
}

void publishMetadata(const Directory& directory, const std::string& name, const Json& document, bool replace) {
    const auto temporary = newName("metadata-");
    const auto file = newFile(directory, temporary);
    const auto bytes = document.dump();
    writeBytes(file, directory.path / temporary, bytes.data(), bytes.size());
    sync(file, directory.path / temporary);
    const auto result = replace ? renameat(directory.fd.get(), temporary.c_str(), directory.fd.get(), name.c_str())
        : renameatx_np(directory.fd.get(), temporary.c_str(), directory.fd.get(), name.c_str(), RENAME_EXCL);
    if (result < 0) systemError(directory.path / name);
    sync(directory);
}

bool validHash(std::string_view hash) {
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

Files fileMap(const Json& document, const char* field) {
    const auto& object = document.at(field);
    if (!object.is_object()) throw std::runtime_error("expected a file hash map");
    auto files = object.get<Files>();
    for (const auto& [name, hash] : files)
        if (!validPath(name) || !validHash(hash)) fail(name, "invalid file metadata");
    return files;
}

Names names(const Json& document, const char* field) {
    const auto items = document.at(field).get<std::vector<std::string>>();
    Names result;
    for (const auto& name : items)
        if (!validPath(name) || !result.insert(name).second) fail(name, "invalid path metadata");
    return result;
}

struct Manifest {
    uint64_t generation;
    std::string identity;
    Files files;
    Names directories;
};

void checkSchema(const Json& document) {
    if (!document.at("schema").is_number_integer() || document.at("schema") != 1)
        throw std::runtime_error("unsupported metadata schema. Reopen the newer VOSC app or restore valid metadata");
}

Manifest readManifest(const Directory& resources) {
    std::string identity;
    const auto document = metadata(resources, "vosc-stock.json", &identity);
    checkSchema(document);
    if (!document.at("generation").is_number_unsigned() || document.at("generation") == 0)
        fail(resources.path / "vosc-stock.json", "invalid stock generation");
    Manifest manifest{document.at("generation").get<uint64_t>(), std::move(identity),
                      fileMap(document, "files"), names(document, "directories")};
    Names normalized;
    auto check = [&](const std::string& name) {
        const auto parent = fs::path(name).parent_path().string();
        if (!normalized.insert(folded(name)).second || (!parent.empty() && !manifest.directories.contains(parent)))
            fail(name, "invalid stock parent or colliding name");
    };
    for (const auto& name : manifest.directories) check(name);
    for (const auto& [name, hash] : manifest.files) check(name);
    return manifest;
}

struct State {
    uint64_t generation = 0;
    std::string manifest;
    std::string transaction;
    Files owned;
    Names directories;
    Names deletedFiles;
    Names deletedDirectories;

    Json json() const {
        return {{"schema", 1}, {"generation", generation}, {"manifest", manifest}, {"transaction", transaction},
                {"owned", owned}, {"directories", directories}, {"deleted_files", deletedFiles},
                {"deleted_directories", deletedDirectories}};
    }

    static State read(const Json& json) {
        checkSchema(json);
        if (!json.at("generation").is_number_unsigned() || json.at("generation") == 0)
            throw std::runtime_error("invalid managed data generation");
        auto identity = json.at("manifest").get<std::string>();
        auto transaction = json.at("transaction").get<std::string>();
        if (!validHash(identity) || !validTransaction(transaction))
            throw std::runtime_error("invalid managed data identity");
        return {json.at("generation").get<uint64_t>(), std::move(identity), std::move(transaction),
                fileMap(json, "owned"), names(json, "directories"),
                names(json, "deleted_files"), names(json, "deleted_directories")};
    }

    static bool validTransaction(std::string_view value) {
        return value.size() == 20 && value.starts_with("txn-")
            && value.substr(4).find_first_not_of("0123456789abcdef") == std::string_view::npos;
    }
};

bool pathExists(const Directory& root, const std::string& name) {
    const auto target = targetOf(root, name);
    if (target.parent.fd) return entry(target.parent, target.name).has_value();
    // An occupied but inaccessible ancestor represents a conflict rather than a free path.
    const auto relative = target.parent.path.lexically_relative(root.path).string();
    if (relative == name || relative.empty()) return false;
    return pathExists(root, relative);
}

bool reconcile(State& state, const Directory& root) {
    bool changed = false;
    auto observe = [&](auto& owned, Names& deleted, auto name) {
        for (auto it = owned.begin(); it != owned.end();) {
            const auto& path = name(*it);
            if (pathExists(root, path)) { ++it; continue; }
            deleted.insert(path);
            it = owned.erase(it);
            changed = true;
        }
        for (auto it = deleted.begin(); it != deleted.end();) {
            if (!pathExists(root, *it)) { ++it; continue; }
            it = deleted.erase(it);
            changed = true;
        }
    };
    observe(state.owned, state.deletedFiles, [](const auto& item) -> const auto& { return item.first; });
    observe(state.directories, state.deletedDirectories, [](const auto& name) -> const auto& { return name; });
    return changed;
}

bool deleted(const State& state, const std::string& name) {
    if (state.deletedFiles.contains(name) || state.deletedDirectories.contains(name)) return true;
    for (auto parent = fs::path(name).parent_path(); !parent.empty(); parent = parent.parent_path())
        if (state.deletedDirectories.contains(parent.string()) || state.deletedFiles.contains(parent.string())) return true;
    return false;
}

void stageFile(const Directory& stock, const Directory& staging, const std::string& name, const std::string& hash) {
    auto source = targetOf(stock, name);
    if (!source.parent.fd) fail(source.parent.path, "missing stock directory");
    const auto input = openFile(source.parent, source.name);
    auto target = targetOf(staging, name, true);
    const auto output = newFile(target.parent, target.name);
    const auto outputPath = target.parent.path / target.name;
    if (sha256(input, source.parent.path / source.name, &output, outputPath) != hash)
        fail(source.parent.path / source.name, "stock hash does not match the signed manifest");
    sync(output, outputPath);
    sync(target.parent);
}

Json identity(const struct stat& status) {
    return Json::array({static_cast<uint64_t>(status.st_dev), static_cast<uint64_t>(status.st_ino)});
}

#ifdef VOSC_MANAGED_DATA_TESTING
void checkpoint(const char* name) {
    if (const char* point = getenv("VOSC_DATA_TEST_CRASH"); point && std::string_view(point) == name) _exit(73);
    if (const char* point = getenv("VOSC_DATA_TEST_STOP"); point && std::string_view(point) == name) {
        unsetenv("VOSC_DATA_TEST_STOP");
        raise(SIGSTOP);
    }
}
#else
void checkpoint(const char*) {}
#endif

void renameEntry(const Directory& from, const std::string& oldName, const Directory& to,
                 const std::string& newName, unsigned flags) {
    if (renameatx_np(from.fd.get(), oldName.c_str(), to.fd.get(), newName.c_str(), flags) < 0)
        systemError(to.path / newName);
    sync(from);
    sync(to);
}

void requireIdentity(const Directory& root, const Json& expected) {
    if (identity(info(root.fd, root.path)) != expected)
        fail(root.path, "data folder changed during a pending transaction. Restore it before retrying");
}

// A pending directory is private and represents one known transaction. Once committed,
// its displaced originals are kept at recovery/<transaction>/files/<relative path>.
void finish(const Directory& control, const Directory& work, const std::string& transaction) {
    const auto recovery = createDir(control, "recovery");
    requireOwned(recovery, true);
    renameEntry(control, work.path.filename(), recovery, transaction, RENAME_EXCL);
}

void warn(std::vector<std::string>& warnings, std::string message) {
    std::fprintf(stderr, "vosc: %s\n", message.c_str());
    if (warnings.size() < 5) warnings.push_back(std::move(message));
}

void apply(const Directory& support, const Directory& control, const Directory& work,
           const Json& journal, std::vector<std::string>& warnings) {
    checkSchema(journal);
    auto next = State::read(journal.at("state"));
    const auto current = entry(control, StateName) ? State::read(metadata(control, StateName)) : State{};
    if (current.transaction == next.transaction) {
        finish(control, work, next.transaction);
        return;
    }
    const auto isIdentity = [](const Json& value) {
        return value.is_array() && value.size() == 2 && value[0].is_number_unsigned()
            && value[1].is_number_unsigned();
    };
    if (current.transaction != journal.at("previous_transaction").get<std::string>()
        || current.generation >= next.generation || !journal.at("seed").is_boolean()
        || !isIdentity(journal.at("root")))
        fail(work.path, "invalid transaction metadata. Restore valid managed data from backup and retry");
    if (journal.at("seed").get<bool>()) {
        if (entry(work, "tree")) {
            const auto tree = openDir(work, "tree");
            requireIdentity(tree, journal.at("root"));
            for (const auto& name : next.directories) {
                const auto target = targetOf(tree, name);
                if (!target.parent.fd) fail(tree.path / name, "missing staged directory");
                openDir(target.parent, target.name);
            }
            for (const auto& [name, hash] : next.owned) {
                const auto target = targetOf(tree, name);
                if (!target.parent.fd || sha256(openFile(target.parent, target.name), tree.path / name) != hash)
                    fail(tree.path / name, "staged seed no longer matches the journal");
            }
            if (entry(support, "data"))
                fail(support.path / "data", "another data folder appeared during setup. Keep a backup of that folder, move it out of the way, and retry");
            renameEntry(work, "tree", support, "data", RENAME_EXCL);
            checkpoint("root_published");
        }
        requireIdentity(openDir(support, "data"), journal.at("root"));
    } else {
        const auto data = openDir(support, "data");
        requireIdentity(data, journal.at("root"));
        const auto files = openDir(work, "files");
        const auto directories = openDir(work, "directories");
        const auto& operations = journal.at("operations");
        if (!operations.is_array()) fail(work.path, "invalid operation list");

        // Verify every journal operation before publishing any remaining entry.
        // A completed rename is proven by the incoming inode at its live path.
        Names operationPaths;
        for (size_t i = 0; i < operations.size(); ++i) {
            const auto& op = operations.at(i);
            const auto name = op.at("path").get<std::string>();
            const auto kind = op.at("kind").get<std::string>();
            if (!validPath(name) || !operationPaths.insert(folded(name)).second
                || (kind != "directory" && kind != "install" && kind != "refresh")
                || !isIdentity(op.at("staged"))
                || (kind != "directory" && !validHash(op.at("hash").get<std::string>()))
                || (kind == "refresh" && (!validHash(op.at("previous").get<std::string>())
                                          || !isIdentity(op.at("previous_inode")))))
                fail(work.path, "invalid pending operation");
            const bool directory = kind == "directory";
            const bool refresh = kind == "refresh";
            const auto source = directory ? Target{duplicate(directories), std::to_string(i)} : targetOf(files, name);
            if (!source.parent.fd) fail(work.path / name, "missing staged parent");
            const auto staged = entry(source.parent, source.name);
            if (staged && identity(*staged) == op.at("staged")) {
                if (directory) {
                    openDir(source.parent, source.name);
                } else if (sha256(openFile(source.parent, source.name), source.parent.path / source.name)
                           != op.at("hash").get<std::string>()) {
                    fail(work.path / name, "staged file no longer matches the journal");
                }
                continue;
            }
            const auto live = targetOf(data, name);
            const auto installed = live.parent.fd ? entry(live.parent, live.name) : std::nullopt;
            if ((!staged && refresh) || (staged && !refresh) || !installed
                || identity(*installed) != op.at("staged"))
                fail(work.path / name, "ambiguous stock publication. Preserve the transaction and restore valid managed data from backup");
        }

        for (size_t i = 0; i < operations.size(); ++i) {
            const auto& op = operations.at(i);
            const auto name = op.at("path").get<std::string>();
            const auto kind = op.at("kind").get<std::string>();
            const bool directory = kind == "directory";
            const bool refresh = kind == "refresh";
            const auto hash = directory ? std::string{} : op.at("hash").get<std::string>();
            const auto previousHash = refresh ? op.at("previous").get<std::string>() : std::string{};
            auto source = directory ? Target{duplicate(directories), std::to_string(i)} : targetOf(files, name);
            if (!source.parent.fd) fail(work.path, "missing staged parent");
            const auto staged = entry(source.parent, source.name);
            const auto backup = control.path / "recovery" / next.transaction / "files" / name;
            const auto live = targetOf(data, name);
            bool published = false;
            const auto installed = live.parent.fd ? entry(live.parent, live.name) : std::nullopt;
            if ((!refresh && !staged) || (refresh && staged && identity(*staged) != op.at("staged"))) {
                if (!installed || identity(*installed) != op.at("staged"))
                    fail(work.path / name, "live file changed during stock recovery. Preserve the transaction and retry with a backup");
                published = true;
            } else if (!staged || identity(*staged) != op.at("staged")) {
                fail(work.path, "staged file identity changed. Preserve the transaction directory and retry with a backup");
            } else if (live.parent.fd) {
                if (refresh) {
                    const auto current = openFile(live.parent, live.name, true);
                    if (current && sha256(current, data.path / name) == previousHash) {
                        const auto observed = entry(live.parent, live.name);
                        if (observed && observed->st_nlink == 1 && identity(*observed) == op.at("previous_inode")
                            && sameFile(*observed, info(current, data.path / name))) {
                            checkpoint("before_exchange");
                            if (renameatx_np(source.parent.fd.get(), source.name.c_str(), live.parent.fd.get(),
                                             live.name.c_str(), RENAME_SWAP) == 0) {
                                sync(source.parent);
                                sync(live.parent);
                                published = true;
                                checkpoint("file_published");
                            } else if (errno == ENOTSUP || errno == EINVAL || errno == EXDEV) {
                                warn(warnings, "Stock update skipped for " + (data.path / name).string()
                                               + ". This volume cannot safely exchange files.");
                            } else if (errno != ENOENT) {
                                systemError(data.path / name);
                            }
                        }
                    }
                } else if (!entry(live.parent, live.name)) {
                    if (renameatx_np(source.parent.fd.get(), source.name.c_str(), live.parent.fd.get(),
                                     live.name.c_str(), RENAME_EXCL) == 0) {
                        sync(source.parent);
                        sync(live.parent);
                        published = true;
                        checkpoint("file_published");
                    } else if (errno != EEXIST) systemError(data.path / name);
                }
            }
            if (!published) continue;
            if (refresh) {
                const auto displacedStatus = entry(source.parent, source.name);
                const auto displaced = openFile(source.parent, source.name, true);
                if (!displacedStatus || !displaced || identity(*displacedStatus) != op.at("previous_inode")) {
                    // An outside editor may replace the pathname between the last
                    // identity check and the exchange. Restore that entry only
                    // when the live name still contains the incoming stock.
                    const auto incoming = openFile(live.parent, live.name, true);
                    if (displacedStatus && incoming && identity(info(incoming, data.path / name)) == op.at("staged")
                        && sha256(incoming, data.path / name) == hash
                        && renameatx_np(source.parent.fd.get(), source.name.c_str(), live.parent.fd.get(),
                                        live.name.c_str(), RENAME_SWAP) == 0) {
                        sync(source.parent);
                        sync(live.parent);
                        warn(warnings, "Stock update skipped after a concurrent change to " + (data.path / name).string());
                        continue;
                    }
                    warn(warnings, "Check " + (data.path / name).string() + " and the item retained at "
                                   + backup.string() + " after a concurrent change.");
                } else if (sha256(displaced, source.parent.path / source.name) != previousHash) {
                    warn(warnings, "An edited original was retained at " + backup.string());
                }
            }
            if (directory) next.directories.insert(name);
            else next.owned[name] = hash;
        }
        reconcile(next, data);
    }
    checkpoint("before_commit");
    publishMetadata(control, StateName, next.json(), entry(control, StateName).has_value());
    checkpoint("state_committed");
    finish(control, work, next.transaction);
}

void transact(const Directory& support, const Directory& control, const Directory& stock,
              const Manifest& manifest, const Directory& data, State next, std::vector<std::string>& warnings) {
    const auto previousTransaction = next.transaction;
    next.generation = manifest.generation;
    next.manifest = manifest.identity;
    next.transaction = newName("txn-");

    const auto stagingName = "stage-" + next.transaction.substr(4);
    if (mkdirat(control.fd.get(), stagingName.c_str(), 0700) < 0) systemError(control.path / stagingName);
    sync(control);
    const auto work = openDir(control, stagingName);
    const auto recovery = createDir(control, "recovery");
    requireOwned(recovery, true);
    Json journal = {{"schema", 1}, {"seed", !data.fd}, {"previous_transaction", previousTransaction},
                    {"operations", Json::array()}};
    if (!data.fd) {
        const auto tree = createDir(work, "tree");
        for (const auto& name : manifest.directories) {
            auto target = targetOf(tree, name, true);
            createDir(target.parent, target.name);
        }
        for (const auto& [name, hash] : manifest.files) stageFile(stock, tree, name, hash);
        next.owned = manifest.files;
        next.directories = manifest.directories;
        journal["root"] = identity(info(tree.fd, tree.path));
    } else {
        journal["root"] = identity(info(data.fd, data.path));
        const auto files = createDir(work, "files");
        const auto directories = createDir(work, "directories");
        auto& operations = journal["operations"];
        Names plannedDirectories;
        for (const auto& name : manifest.directories) {
            if (deleted(next, name) || next.directories.contains(name)) continue;
            const auto target = targetOf(data, name);
            if (target.parent.fd && entry(target.parent, target.name)) {
                // Observing a stock directory records future deletions. Its files
                // remain individually owned or unowned.
                if (openDir(target.parent, target.name, true).fd) next.directories.insert(name);
                continue;
            }
            const auto parent = fs::path(name).parent_path().string();
            if (!target.parent.fd && !plannedDirectories.contains(parent)) continue;
            const auto staging = createDir(directories, std::to_string(operations.size()));
            operations.push_back({{"path", name}, {"kind", "directory"}, {"staged", identity(info(staging.fd, staging.path))}});
            plannedDirectories.insert(name);
        }
        for (const auto& [name, hash] : manifest.files) {
            if (deleted(next, name)) continue;
            const auto target = targetOf(data, name);
            const auto parent = fs::path(name).parent_path().string();
            if (!target.parent.fd && !plannedDirectories.contains(parent)) continue;
            const auto baseline = next.owned.find(name);
            const bool exists = target.parent.fd && entry(target.parent, target.name).has_value();
            // A managed file may disappear between reconciliation and planning.
            if (!exists && baseline != next.owned.end()) continue;
            Json op = {{"path", name}, {"hash", hash}, {"kind", "install"}};
            if (exists) {
                if (baseline == next.owned.end() || baseline->second == hash) continue;
                const auto current = openFile(target.parent, target.name, true);
                if (!current || sha256(current, data.path / name) != baseline->second) continue;
                op["kind"] = "refresh";
                op["previous"] = baseline->second;
                op["previous_inode"] = identity(info(current, data.path / name));
            }
            stageFile(stock, files, name, hash);
            const auto source = targetOf(files, name);
            op["staged"] = identity(*entry(source.parent, source.name));
            operations.push_back(std::move(op));
        }
    }
    journal["state"] = next.json();
    // No live path changes occur before this durable intent.
    checkpoint("before_journal");
    publishMetadata(work, JournalName, journal, false);
    // A stage-* directory can never contain displaced user data. Promote it
    // before any live mutation so the missing journal case stays recoverable.
    renameEntry(control, stagingName, control, next.transaction, RENAME_EXCL);
    checkpoint("journal_published");
    apply(support, control, openDir(control, next.transaction), journal, warnings);
}

void discardUnpublished(const Directory& parent, const std::string& name) {
    const auto status = entry(parent, name);
    if (!status) return;
    const auto path = parent.path / name;
    if (S_ISDIR(status->st_mode)) {
        const auto directory = openDir(parent, name);
        requireOwned(directory, true);
        Fd fd(openat(directory.fd.get(), ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
        if (!fd) systemError(path);
        auto* stream = fdopendir(fd.get());
        if (!stream) systemError(path);
        fd.release();
        const std::unique_ptr<DIR, decltype(&closedir)> owned(stream, closedir);
        while (true) {
            errno = 0;
            const auto* child = readdir(stream);
            if (!child) {
                if (errno) systemError(path);
                break;
            }
            const std::string childName = child->d_name;
            if (childName != "." && childName != "..") discardUnpublished(directory, childName);
        }
    } else if (!S_ISREG(status->st_mode) || status->st_nlink != 1 || status->st_uid != getuid()) {
        fail(path, "unexpected item in unpublished setup. Back up that transaction directory and move it before retrying");
    }
    const auto after = entry(parent, name);
    if (!after || !sameFile(*status, *after)) fail(path, "unpublished setup changed during cleanup");
    if (unlinkat(parent.fd.get(), name.c_str(), S_ISDIR(status->st_mode) ? AT_REMOVEDIR : 0) < 0)
        systemError(path);
}

std::optional<Directory> pending(const Directory& control, bool exclusive) {
    Fd fd(openat(control.fd.get(), ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!fd) systemError(control.path);
    auto* stream = fdopendir(fd.get());
    if (!stream) systemError(control.path);
    fd.release();
    const std::unique_ptr<DIR, decltype(&closedir)> directory(stream, closedir);
    std::optional<Directory> result;
    while (true) {
        errno = 0;
        const auto* file = readdir(stream);
        if (!file) {
            if (errno) systemError(control.path);
            return result;
        }
        const std::string name = file->d_name;
        if (name.starts_with("stage-") && State::validTransaction("txn-" + name.substr(6))) {
            if (exclusive) {
                discardUnpublished(control, name);
                sync(control);
            }
            continue;
        }
        if (!State::validTransaction(name)) continue;
        auto work = openDir(control, name);
        requireOwned(work, true);
        if (!entry(work, JournalName))
            fail(work.path, "missing journal for active stock transaction. Preserve its recovery files and restore valid state");
        if (result) fail(control.path, "multiple pending transactions. Restore valid managed data from backup");
        result = std::move(work);
    }
}

using LockClock = std::chrono::steady_clock;
constexpr auto LockTimeout = std::chrono::seconds(2);

void waitForLock(LockClock::time_point deadline, const fs::path& path) {
    if (LockClock::now() >= deadline)
        fail(path, "another VOSC window is using the data folder. Quit the other window and retry");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
}

bool tryLock(const Fd& lock, int mode, const fs::path& path) {
    if (flock(lock.get(), mode | LOCK_NB) == 0) return true;
    if (errno != EWOULDBLOCK && errno != EINTR) systemError(path);
    return false;
}

Fd openLock(const Directory& control) {
    const auto path = control.path / "lock";
    const auto deadline = LockClock::now() + LockTimeout;
    constexpr int flags = O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW;
    while (true) {
        Fd lock(openat(control.fd.get(), "lock", flags | O_CREAT | O_EXCL, 0600));
        if (lock) return lock;
        if (errno == EEXIST) lock = Fd(openat(control.fd.get(), "lock", flags));
        if (lock) return lock;
        if (errno != ENOENT && errno != EINTR) systemError(path);
        waitForLock(deadline, path);
    }
}

struct Prepared {
    fs::path path;
    Fd lock;
};

Prepared prepare(const fs::path& applicationSupport, const fs::path& resourcesPath, std::vector<std::string>& warnings) {
    const auto homePath = fs::canonical(applicationSupport.parent_path().parent_path());
    const Directory home{Fd(open(homePath.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW)), homePath};
    if (!home.fd) systemError(home.path);
    const auto library = createDir(home, applicationSupport.parent_path().filename());
    const auto support = createDir(createDir(library, applicationSupport.filename()), "VOSC");
    requireOwned(support);
    const auto control = createDir(support, ControlName);
    requireOwned(control, true);
    const auto lockPath = control.path / "lock";
    auto lock = openLock(control);
    const auto status = info(lock, lockPath);
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1 || status.st_uid != getuid()
        || !exactName(control, "lock") || (status.st_mode & (S_IRWXG | S_IRWXO))) fail(lockPath, "invalid lock file");

    const Directory resources{Fd(open(resourcesPath.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW)), resourcesPath};
    if (!resources.fd) systemError(resources.path);
    const auto manifest = readManifest(resources);
    const auto stock = openDir(resources, "data");

    // Check again after each lock transition. Taking an exclusive lock while still
    // holding a shared lock would leave concurrent first launches waiting on one another.
    int mode = LOCK_SH;
    auto deadline = LockClock::now() + LockTimeout;
    for (;;) {
        if (!tryLock(lock, mode, control.path)) {
            waitForLock(deadline, control.path);
            // An initializer may have committed while we were waiting for
            // exclusive access, and now holds its renderer's shared lock.
            mode = LOCK_SH;
            continue;
        }
        if (const auto work = pending(control, mode == LOCK_EX)) {
            if (mode == LOCK_EX) {
                const auto journal = metadata(*work, JournalName);
                if (State::read(journal.at("state")).transaction != work->path.filename())
                    fail(work->path, "transaction identity mismatch");
                apply(support, control, *work, journal, warnings);
            }
        } else {
            const bool hasState = entry(control, StateName).has_value();
            State state = hasState ? State::read(metadata(control, StateName)) : State{};
            const auto data = openDir(support, "data", true);
            if (!data.fd && (hasState || entry(support, "data"))) {
                fail(data.path, hasState ? "managed data is missing or unavailable. Restore the data folder from backup and retry"
                                         : "expected a writable data directory. Move the conflicting item and retry");
            }
            if (data.fd) requireOwned(data);
            if (state.generation == manifest.generation && state.manifest != manifest.identity)
                fail(resources.path / "vosc-stock.json", "distinct stock bundles share the same generation");
            const bool changed = data.fd && reconcile(state, data);
            const bool needsUpdate = !hasState || state.generation < manifest.generation;
            if (mode == LOCK_SH && !needsUpdate && !changed)
                return {data.path, std::move(lock)};
            if (mode == LOCK_EX) {
                if (needsUpdate) transact(support, control, stock, manifest, data, std::move(state), warnings);
                else if (changed) publishMetadata(control, StateName, state.json(), true);
            }
        }
        if (flock(lock.get(), LOCK_UN) < 0) systemError(lockPath);
        if (mode == LOCK_EX) deadline = LockClock::now() + LockTimeout;
        mode = mode == LOCK_SH ? LOCK_EX : LOCK_SH;
    }
}

Fd rendererLock;
fs::path writableRoot;

NSString* displayString(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()] ?: @"The data folder is unavailable.";
}

bool retrySetup(const std::string& message) {
    [NSApplication sharedApplication];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleCritical;
    alert.messageText = @"VOSC couldn’t prepare the data folder";
    alert.informativeText = displayString(message);
    [alert addButtonWithTitle:@"Retry"];
    [alert addButtonWithTitle:@"Quit"];
    return [alert runModal] == NSAlertFirstButtonReturn;
}

} // namespace

bool initializeManagedData(bool interactive) {
    @autoreleasepool {
        NSBundle* bundle = [NSBundle mainBundle];
        id marker = [bundle objectForInfoDictionaryKey:@"VOSCDirectDownload"];
        if (!marker || CFGetTypeID((__bridge CFTypeRef)marker) != CFBooleanGetTypeID() || ![marker boolValue])
            return false;
        NSURL* url = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
            inDomains:NSUserDomainMask].firstObject;
        const auto support = [url fileSystemRepresentation];
        const auto resources = [bundle.resourceURL fileSystemRepresentation];
        if (!support || !resources) throw std::runtime_error("could not locate VOSC Application Support or bundled resources");
        std::vector<std::string> warnings;
        while (true) {
            try {
                auto result = prepare(support, resources, warnings);
                rendererLock = std::move(result.lock);
                writableRoot = std::move(result.path);
                break;
            } catch (const std::exception& error) {
                const auto message = (fs::path(support) / "VOSC/data").string() + "\n\n" + error.what();
                if (!interactive || !retrySetup(message)) throw std::runtime_error(message);
            }
        }
        if (interactive && !warnings.empty()) {
            [NSApplication sharedApplication];
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"Some stock files need your attention";
            std::string message;
            for (const auto& warning : warnings) message += warning + "\n";
            alert.informativeText = displayString(message);
            [alert addButtonWithTitle:@"Continue"];
            [alert runModal];
        }
        return true;
    }
}

const fs::path& managedDataRoot() { return writableRoot; }

} // namespace vosc::macos
