import CoreFoundation
import Foundation
import Network
import Security

typealias WireObject = [String: Any]
let wireVersion = 1
let wireLimit = 256 * 1024

struct WireError: Error {
    let code: String
    let message: String
    let diagnostics: Any?

    init(_ code: String, _ message: String, diagnostics: Any? = nil) {
        self.code = code
        self.message = message
        self.diagnostics = diagnostics
    }

    var fields: WireObject {
        var result: WireObject = ["code": code, "message": message]
        if let diagnostics = diagnostics { result["diagnostics"] = diagnostics }
        return result
    }
}

func wireNumber(_ value: Any?) -> Double? {
    guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
    return number.doubleValue.isFinite ? number.doubleValue : nil
}

func wireInt(_ value: Any?) -> Int? {
    guard let number = wireNumber(value), number.rounded() == number, abs(number) <= 9_007_199_254_740_991
    else { return nil }
    return Int(number)
}

struct Request {
    let id: String
    let method: String
    let target: String?
    let epoch: String?
    let revision: Int?
    let params: WireObject
    let bytes: Data

    init(_ bytes: Data) throws {
        guard let object = try JSONSerialization.jsonObject(with: bytes) as? WireObject,
              let id = object["id"] as? String, (1...64).contains(id.utf8.count),
              id.range(of: "^[a-zA-Z0-9_-]+$", options: .regularExpression) != nil,
              let method = object["method"] as? String, method.utf8.count <= 32,
              let params = object["params"] as? WireObject,
              Set(object.keys).isSubset(of: ["id", "method", "version", "params", "target", "epoch", "revision"]),
              object["target"] == nil || object["target"] is String,
              object["epoch"] == nil || object["epoch"] is String,
              object["revision"] == nil || wireInt(object["revision"]) != nil
        else { throw WireError("invalid_request", "Malformed draft request") }
        guard wireInt(object["version"]) == wireVersion else {
            throw WireError("incompatible_protocol", "This host requires protocol version \(wireVersion)")
        }
        self.id = id
        self.method = method
        self.params = params
        self.target = object["target"] as? String
        self.epoch = object["epoch"] as? String
        self.revision = wireInt(object["revision"])
        self.bytes = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
    }

    func require(_ keys: Set<String>, optional: Set<String> = []) throws {
        let actual = Set(params.keys)
        guard keys.isSubset(of: actual), actual.isSubset(of: keys.union(optional)) else {
            throw WireError("invalid_request", "Unexpected or missing fields for \(method)")
        }
    }
}

final class WireSession {
    let id = UUID()
    private let connection: NWConnection
    private let queue: DispatchQueue
    private let requiresClient: Bool
    private let handled: (WireSession, Request) -> Void
    private let closed: (UUID) -> Void
    private var buffered = Data()
    private var deadline: DispatchWorkItem?
    private var running = true
    private(set) var peer: String?

    init(_ connection: NWConnection, queue: DispatchQueue, requiresClient: Bool,
         handled: @escaping (WireSession, Request) -> Void, closed: @escaping (UUID) -> Void) {
        self.connection = connection
        self.queue = queue
        self.requiresClient = requiresClient
        self.handled = handled
        self.closed = closed
    }

    func start() {
        connection.stateUpdateHandler = { [weak self] state in
            guard let self = self else { return }
            switch state {
            case .ready:
                if requiresClient {
                    guard let tls = connection.metadata(definition: NWProtocolTLS.definition) as? NWProtocolTLS.Metadata
                    else { stop(); return }
                    var leaf: SecCertificate?
                    sec_protocol_metadata_access_peer_certificate_chain(tls.securityProtocolMetadata) { certificate in
                        if leaf == nil { leaf = sec_certificate_copy_ref(certificate).takeRetainedValue() }
                    }
                    guard let leaf = leaf else { stop(); return }
                    peer = sha256Hex(SecCertificateCopyData(leaf) as Data)
                }
                armDeadline()
                receive()
            case .failed, .cancelled:
                stop()
            default:
                break
            }
        }
        armDeadline()
        connection.start(queue: queue)
    }

    func stop() {
        guard running else { return }
        running = false
        deadline?.cancel()
        connection.stateUpdateHandler = nil
        connection.cancel()
        closed(id)
    }

    private func armDeadline() {
        deadline?.cancel()
        let limit = DispatchWorkItem { [weak self] in self?.stop() }
        deadline = limit
        queue.asyncAfter(deadline: .now() + 15, execute: limit)
    }

    private func receive() {
        guard running else { return }
        if let end = buffered.firstIndex(of: 0x0a) {
            guard end - buffered.startIndex <= wireLimit else { stop(); return }
            let line = Data(buffered[..<end])
            buffered = Data(buffered[buffered.index(after: end)...])
            do {
                let request = try Request(line)
                armDeadline()
                handled(self, request)
            } catch {
                let id = ((try? JSONSerialization.jsonObject(with: line)) as? WireObject)?["id"] as? String
                guard let id = id, id.utf8.count <= 64 else { stop(); return }
                respond(["id": id, "ok": false, "error": (error as? WireError
                    ?? WireError("invalid_request", "Malformed draft request")).fields])
            }
            return
        }
        guard buffered.count <= wireLimit else { stop(); return }
        connection.receive(minimumIncompleteLength: 1, maximumLength: 32768) { [weak self] bytes, _, complete, error in
            guard let self = self else { return }
            if let bytes = bytes, !bytes.isEmpty {
                buffered.append(bytes)
                receive()
            } else if complete || error != nil {
                stop()
            } else {
                receive()
            }
        }
    }

    func respond(_ body: WireObject) {
        guard running else { return }
        let bounded: Data
        if let bytes = try? JSONSerialization.data(withJSONObject: body), bytes.count <= wireLimit {
            bounded = bytes
        } else if let id = body["id"] as? String,
                  let error = try? JSONSerialization.data(withJSONObject: [
                    "id": id, "ok": false, "error": ["code": "response_limit", "message": "Host response exceeded its limit"],
                  ]) {
            bounded = error
        } else { stop(); return }
        connection.send(content: bounded + Data([0x0a]), completion: .contentProcessed { [weak self] error in
            guard let self = self else { return }
            if error != nil { stop() } else { armDeadline(); receive() }
        })
    }
}
