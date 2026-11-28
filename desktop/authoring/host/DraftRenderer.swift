import CoreFoundation
import Darwin
import Foundation

private let sourceLimit = 128 * 1024
private let vertexAssets = [
    "shaders/passthru.vert",
    "shaders/include/of_default_uniforms.glsl",
    "shaders/include/of_default_vertex_in_attributes.glsl",
]

struct Fragment {
    let base64: String
    let digest: String

    init(_ request: Request, wgsl: Bool = false) throws {
        guard let encoded = request.params["source"] as? String,
              encoded.utf8.count <= 4 * ((sourceLimit + 2) / 3),
              let bytes = Data(base64Encoded: encoded), !bytes.isEmpty, bytes.count <= sourceLimit,
              let source = String(data: bytes, encoding: .utf8), !bytes.contains(0),
              let claimed = request.params["sha256"] as? String, claimed == sha256Hex(bytes) else {
            throw WireError("invalid_source", "Expected bounded UTF-8 fragment bytes with matching SHA-256")
        }
        guard wgsl || (source.range(of: #"(?im)^\h*#\h*(?:include|pragma\h+include)\b"#,
                           options: .regularExpression) == nil &&
              source.range(of: #"\\\r?\n"#, options: .regularExpression) == nil) else {
            throw WireError("unsupported_dependency", "File includes and continued source directives are unsupported")
        }
        base64 = encoded
        digest = claimed
    }
}

final class DraftRenderer {
    let process: Process
    private(set) var supportsDraftWgsl = false
    private(set) var supportsDraftWebgpuAuto = false
    private let root: URL
    private let controlPath: String
    private let logPath: URL
    private let log: FileHandle

    init(validation: Bool) throws {
        let bundle = Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent("vosc.app")
        let binary = bundle.appendingPathComponent("Contents/MacOS/vosc")
        let stock = bundle.appendingPathComponent("Contents/Resources/data", isDirectory: true)
        let manager = FileManager.default
        guard manager.isExecutableFile(atPath: binary.path) else {
            throw WireError("renderer_missing", "Install vosc.app beside VOSC Render Host.app before enabling previews")
        }
        let root = URL(fileURLWithPath: "/tmp/vosc-draft-\(UUID().uuidString.lowercased())", isDirectory: true)
        let data = root.appendingPathComponent("data", isDirectory: true)
        try manager.createDirectory(at: root, withIntermediateDirectories: false,
                                    attributes: [.posixPermissions: 0o700])
        let process = Process()
        self.root = root
        self.process = process
        controlPath = root.appendingPathComponent("control.sock").path
        logPath = root.appendingPathComponent("renderer.log")
        do {
            try manager.createDirectory(at: data, withIntermediateDirectories: false,
                                        attributes: [.posixPermissions: 0o700])
            for path in vertexAssets {
                let target = data.appendingPathComponent(path)
                try manager.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
                try manager.copyItem(at: stock.appendingPathComponent(path), to: target)
            }
            guard manager.createFile(atPath: logPath.path, contents: Data(), attributes: [.posixPermissions: 0o600])
            else { throw WireError("renderer_startup", "Could not allocate the draft process log") }
            log = try FileHandle(forWritingTo: logPath)
        } catch {
            try? manager.removeItem(at: root)
            throw error
        }
        process.executableURL = binary
        process.arguments = ["--draft-data-root", data.path, "--size", validation ? "160x120" : "640x480", "--fps", "30"]
        if !validation { process.arguments?.append(contentsOf: ["--draft-preview"]) }
        process.currentDirectoryURL = root
        var env = ProcessInfo.processInfo.environment.filter { !$0.key.hasPrefix("VOSC_") }
        env["VOSC_CONTROL_SOCKET"] = controlPath
        env["VOSC_DRAFT_SHADER_TOOLS"] = bundle.appendingPathComponent("Contents/Resources/webgpu-tools",
                                                                     isDirectory: true).path
        process.environment = env
        process.standardInput = FileHandle.nullDevice
        process.standardOutput = log
        process.standardError = log
        do {
            try process.run()
            let started = DispatchTime.now().uptimeNanoseconds
            while !manager.fileExists(atPath: controlPath), process.isRunning,
                  DispatchTime.now().uptimeNanoseconds - started < 6_000_000_000 {
                Thread.sleep(forTimeInterval: 0.03)
            }
            guard manager.fileExists(atPath: controlPath) else {
                let detail = (try? String(contentsOf: logPath, encoding: .utf8))?.suffix(1200) ?? ""
                throw WireError("renderer_startup", "Private renderer did not open its control socket. \(detail)")
            }
            let capabilities = try query("get_capabilities")
            guard wireInt(capabilities["version"]) == 1, capabilities["transport"] as? String == "unix-ndjson",
                  let operations = capabilities["operations"] as? [String],
                  ["configure_layers", "publish_shader", "get_scene"].allSatisfy(operations.contains) else {
                throw WireError("renderer_capabilities", "Installed VOSC cannot handle private draft operations")
            }
            if !validation {
                guard ["draft_prepare", "draft_commit", "draft_cancel", "draft_fence", "draft_sample", "draft_callback",
                       "draft_configure_slot", "draft_bind_uniform"].allSatisfy(operations.contains) else {
                    throw WireError("renderer_capabilities", "Install the matching VOSC renderer to run draft transactions")
                }
            }
            let features = capabilities["features"] as? WireObject
            supportsDraftWgsl = features?["draft_wgsl"] as? Bool == true
            if let auto = features?["draft_webgpu_auto"] as? NSNumber {
                supportsDraftWebgpuAuto = CFGetTypeID(auto) == CFBooleanGetTypeID() && auto.boolValue
            }
            _ = try mutate("configure_layers", ["count": 1, "layout": "stack"])
        } catch {
            stop()
            throw error
        }
    }

    deinit { stop() }

    func stop() {
        if process.isRunning {
            process.terminate()
            for _ in 0..<25 where process.isRunning { Thread.sleep(forTimeInterval: 0.02) }
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
            process.waitUntilExit()
        }
        try? log.close()
        try? FileManager.default.removeItem(at: root)
    }

    func query(_ method: String, _ params: WireObject = [:], until: UInt64? = nil) throws -> WireObject {
        let result = try exchange(method, params, id: UUID().uuidString, deadline: until ?? deadline())
        guard let data = result["result"] as? WireObject else {
            throw WireError("renderer_protocol", "Native renderer did not return a query result")
        }
        return data
    }

    func mutate(_ method: String, _ params: WireObject, id: String = UUID().uuidString,
                expecting status: String = "rendered") throws -> WireObject {
        let backend = params["backend"] as? String
        let webgpuPrepare = method == "draft_prepare" &&
            (params["language"] as? String == "wgsl" || backend == "auto" || backend == "webgpu")
        let until = webgpuPrepare ?
            DispatchTime.now().uptimeNanoseconds + 4_500_000_000 : deadline()
        let response = try exchange(method, params, id: id, deadline: until)
        return try settle(response, id: id, expecting: status, until: until)
    }

    func recover(_ id: String, expecting status: String) throws -> WireObject {
        let until = deadline()
        let response = try query("get_request", ["request_id": id], until: until)
        return try settle(response, id: id, expecting: status, until: until)
    }

    private func settle(_ initial: WireObject, id: String, expecting status: String, until: UInt64) throws -> WireObject {
        var response = initial
        while ["queued", "applied", "rolling_back"].contains(response["status"] as? String ?? "") {
            Thread.sleep(forTimeInterval: 0.02)
            response = try query("get_request", ["request_id": id], until: until)
        }
        guard response["id"] as? String == id else {
            throw WireError("renderer_protocol", "Native renderer returned a different request receipt")
        }
        try check(response)
        guard response["status"] as? String == status,
              status != "rendered" || (wireInt(response["frame"]) != nil && wireInt(response["revision"]) != nil) else {
            throw WireError("renderer_protocol", "Native renderer did not return a matching \(status) receipt")
        }
        return response
    }

    private func deadline() -> UInt64 { DispatchTime.now().uptimeNanoseconds + 1_850_000_000 }

    private func check(_ response: WireObject) throws {
        guard response["ok"] as? Bool == true else {
            let error = response["error"] as? WireObject
            let lines = error?["diagnostics"] as? [WireObject] ?? []
            throw WireError(error?["code"] as? String ?? "renderer_protocol",
                            error?["message"] as? String ?? "Native renderer returned a failed receipt",
                            diagnostics: lines.isEmpty ? nil : Array(lines.prefix(12)))
        }
    }

    private func exchange(_ method: String, _ params: WireObject, id: String, deadline: UInt64) throws -> WireObject {
        let descriptor = socket(AF_UNIX, SOCK_STREAM, 0)
        guard descriptor >= 0 else { throw WireError("renderer_transport", "Could not create private socket") }
        defer { close(descriptor) }
        var one: Int32 = 1
        _ = setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))

        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(controlPath.utf8CString)
        let copied = withUnsafeMutableBytes(of: &address.sun_path) { destination -> Bool in
            guard bytes.count <= destination.count else { return false }
            for (index, value) in bytes.enumerated() { destination[index] = UInt8(bitPattern: value) }
            return true
        }
        guard copied else { throw WireError("renderer_transport", "Private socket path is too long") }
        let addressLength = MemoryLayout<sockaddr_un>.size
        address.sun_len = UInt8(addressLength)
        guard fcntl(descriptor, F_SETFL, O_NONBLOCK) >= 0 else {
            throw WireError("renderer_transport", "Could not configure the private socket")
        }
        let result = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { connect(descriptor, $0, socklen_t(addressLength)) }
        }
        if result != 0 {
            guard errno == EINPROGRESS else {
                throw WireError("renderer_transport", "Could not reach the owned renderer")
            }
            try available(descriptor, POLLOUT, deadline)
            var status: Int32 = 0
            var size = socklen_t(MemoryLayout<Int32>.size)
            guard getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &status, &size) == 0, status == 0 else {
                throw WireError("renderer_transport", "Native renderer socket rejected connection")
            }
        }

        let request = try JSONSerialization.data(withJSONObject: ["id": id, "method": method, "params": params],
                                                 options: [.withoutEscapingSlashes]) + Data([10])
        guard request.count <= wireLimit else {
            throw WireError("renderer_transport", "Native request exceeds the private socket limit")
        }
        try request.withUnsafeBytes { buffer in
            var offset = 0
            while offset < buffer.count {
                try available(descriptor, POLLOUT, deadline)
                let count = send(descriptor, buffer.baseAddress!.advanced(by: offset), buffer.count - offset, 0)
                if count < 0, errno == EAGAIN || errno == EINTR { continue }
                guard count > 0 else { throw WireError("renderer_transport", "Native request write failed") }
                offset += count
            }
        }
        var temporary = [UInt8](repeating: 0, count: 16_384)
        var response = Data()
        while response.count <= 1024 * 1024 {
            try available(descriptor, POLLIN, deadline)
            let count = recv(descriptor, &temporary, temporary.count, 0)
            if count < 0, errno == EAGAIN || errno == EINTR { continue }
            guard count > 0 else { throw WireError("renderer_transport", "Native renderer closed its socket") }
            response.append(contentsOf: temporary.prefix(count))
            if let end = response.firstIndex(of: 10) {
                guard let object = try JSONSerialization.jsonObject(with: Data(response[..<end])) as? WireObject,
                      object["id"] as? String == id else {
                    throw WireError("renderer_protocol", "Native response does not match its private request")
                }
                try check(object)
                return object
            }
        }
        throw WireError("renderer_transport", "Native response exceeded its private limit")
    }

    private func available(_ descriptor: Int32, _ events: Int32, _ deadline: UInt64) throws {
        while DispatchTime.now().uptimeNanoseconds < deadline {
            let now = DispatchTime.now().uptimeNanoseconds
            let milliseconds = Int32(min((deadline > now ? deadline - now : 0) / 1_000_000, 1850))
            var item = pollfd(fd: descriptor, events: Int16(events), revents: 0)
            let count = poll(&item, 1, max(1, milliseconds))
            if count > 0 { return }
            if count < 0, errno != EINTR { break }
        }
        throw WireError("renderer_timeout", "Native renderer did not respond before the request deadline")
    }
}
