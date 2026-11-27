import Foundation
import Network
import Security

struct PairingEntry {
    let request: String
    let name: String
    let fingerprint: String
    let expiration: Date
    var state = "pending"
}

struct HostView {
    let active: Bool
    let stopping: Bool
    let status: String
    let target: String
    let fingerprint: String?
    let port: UInt16?
    let pending: [PairingEntry]
    let paired: [String: String]
}

final class HostService {
    private let queue = DispatchQueue(label: "com.vosc.render-host.network")
    private let draftQueue = DispatchQueue(label: "com.vosc.render-host.draft")
    private let preferences = UserDefaults.standard
    private let target: String
    private let draft: Draft
    private let updated: (HostView) -> Void
    private var identity: HostIdentity?
    private var bootstrap: NWListener?
    private var authenticated: NWListener?
    private var sessions: [UUID: WireSession] = [:]
    private var pairing: [String: PairingEntry] = [:]
    private var approved: [String: String]
    private var status = "Draft service is off"
    private var stopping = false
    private var busy = false
    private var pendingRequest: String?
    private var receipts: [String: (Data, WireObject)] = [:]
    private var receiptOrder: [String] = []
    private let mutations: Set<String> = ["activate", "slot.configure", "uniform.set"]

    init(updated: @escaping (HostView) -> Void) {
        let previous = preferences.string(forKey: "draft-host-target")
        target = previous.flatMap { UUID(uuidString: $0)?.uuidString.lowercased() } ?? UUID().uuidString.lowercased()
        preferences.set(target, forKey: "draft-host-target")
        approved = preferences.dictionary(forKey: "draft-host-approved") as? [String: String] ?? [:]
        draft = Draft(target: target, queue: draftQueue)
        self.updated = updated
        draft.onReset = { [weak self] in
            self?.queue.async { [weak self] in
                self?.receipts.removeAll()
                self?.receiptOrder.removeAll()
            }
        }
        publish()
    }

    func enable() {
        queue.async { [self] in
            guard bootstrap == nil, !stopping else { return }
            do {
                let identity = try HostIdentity.load()
                if let prior = preferences.string(forKey: "draft-host-leaf"), prior != identity.fingerprint {
                    approved.removeAll()
                    saveApproved()
                }
                preferences.set(identity.fingerprint, forKey: "draft-host-leaf")
                self.identity = identity
                bootstrap = try listen(identity, requireClient: false)
                authenticated = try listen(identity, requireClient: true)
                bootstrap?.service = NWListener.Service(name: "VOSC \(target.prefix(8))", type: "_vosc-draft._tcp",
                    domain: nil, txtRecord: NWTXTRecord(["role": "draft", "version": String(wireVersion), "target": target]))
                status = "Starting draft service…"
                bootstrap?.start(queue: queue)
                authenticated?.start(queue: queue)
            } catch {
                disableListeners()
                status = (error as? WireError)?.message ?? error.localizedDescription
            }
            publish()
        }
    }

    func disable(_ completion: (() -> Void)? = nil) {
        queue.async { [self] in
            disableListeners()
            stopping = true
            status = "Stopping private previews…"
            publish()
            draftQueue.async { [self] in
                draft.reset()
                queue.async { [self] in
                    stopping = false
                    status = "Draft service is off"
                    publish()
                    if let completion = completion { DispatchQueue.main.async(execute: completion) }
                }
            }
        }
    }

    func decide(_ request: String, approve: Bool) {
        queue.async { [self] in
            guard var entry = pairing[request], entry.state == "pending", entry.expiration > Date(),
                  bootstrap != nil else { publish(); return }
            if approve {
                guard approved.count < 32 || approved[entry.fingerprint] != nil else {
                    status = "Revoke a controller before approving another"
                    publish()
                    return
                }
                approved[entry.fingerprint] = entry.name
                saveApproved()
            }
            entry.state = approve ? "approved" : "denied"
            pairing[request] = entry
            publish()
        }
    }

    func revoke(_ fingerprint: String) {
        queue.async { [self] in
            approved.removeValue(forKey: fingerprint)
            saveApproved()
            for session in Array(sessions.values) where session.peer == fingerprint { session.stop() }
            pairing = pairing.filter { $0.value.fingerprint != fingerprint }
            let prefix = fingerprint + ":"
            receipts = receipts.filter { !$0.key.hasPrefix(prefix) }
            receiptOrder.removeAll { $0.hasPrefix(prefix) }
            publish()
        }
    }

    private func saveApproved() { preferences.set(approved, forKey: "draft-host-approved") }

    private func disableListeners() {
        for listener in [bootstrap, authenticated].compactMap({ $0 }) {
            listener.stateUpdateHandler = nil
            listener.cancel()
        }
        bootstrap = nil
        authenticated = nil
        for session in Array(sessions.values) { session.stop() }
        pairing.removeAll()
        receipts.removeAll()
        receiptOrder.removeAll()
    }

    private func publish() {
        let current = HostView(active: bootstrap != nil, stopping: stopping, status: status, target: target,
            fingerprint: identity?.fingerprint, port: bootstrap?.port?.rawValue,
            pending: pairing.values.filter { $0.state == "pending" && $0.expiration > Date() }
                .sorted { $0.expiration < $1.expiration },
            paired: approved)
        DispatchQueue.main.async { [updated] in updated(current) }
    }

    private func listen(_ identity: HostIdentity, requireClient: Bool) throws -> NWListener {
        let tls = NWProtocolTLS.Options()
        let options = tls.securityProtocolOptions
        guard let reference = sec_identity_create(identity.identity) else {
            throw WireError("keychain", "Could not create a TLS identity from Keychain")
        }
        sec_protocol_options_set_local_identity(options, reference)
        sec_protocol_options_set_min_tls_protocol_version(options, .TLSv13)
        sec_protocol_options_set_tls_resumption_enabled(options, false)
        sec_protocol_options_set_peer_authentication_required(options, requireClient)
        if requireClient {
            sec_protocol_options_set_verify_block(options, { [weak self] _, trust, complete in
                guard let self = self,
                      let leaf = (SecTrustCopyCertificateChain(sec_trust_copy_ref(trust).takeRetainedValue()) as? [SecCertificate])?.first
                else { complete(false); return }
                let fingerprint = sha256Hex(SecCertificateCopyData(leaf) as Data)
                complete(approved[fingerprint] != nil)
            }, queue)
        }
        let listener = try NWListener(using: NWParameters(tls: tls, tcp: NWProtocolTCP.Options()))
        listener.stateUpdateHandler = { [weak self, weak listener] state in
            guard let self = self, let listener = listener,
                  listener === bootstrap || listener === authenticated else { return }
            switch state {
            case .ready:
                if bootstrap?.port != nil && authenticated?.port != nil { status = "Draft service is ready" }
            case .waiting(let error):
                status = denied(error) ? "Local network permission denied. Check System Settings." : "Waiting for local network: \(error)"
            case .failed(let error):
                status = denied(error) ? "Local network permission denied. Check System Settings." : "Draft listener failed: \(error)"
                disableListeners()
                draftQueue.async { self.draft.reset() }
            default:
                break
            }
            publish()
        }
        listener.newConnectionHandler = { [weak self, weak listener] connection in
            guard let self = self, let listener = listener,
                  listener === bootstrap || listener === authenticated, sessions.count < 16 else {
                connection.cancel()
                return
            }
            let session = WireSession(connection, queue: queue, requiresClient: requireClient,
                handled: { [weak self] connection, request in
                    self?.handle(connection, request, authenticated: requireClient)
                },
                closed: { [weak self] id in self?.sessions.removeValue(forKey: id) })
            sessions[session.id] = session
            session.start()
        }
        return listener
    }

    private func denied(_ error: NWError) -> Bool {
        switch error {
        case .posix(.EPERM), .posix(.EACCES), .dns(-65570): return true
        default: return false
        }
    }

    private func handle(_ session: WireSession, _ request: Request, authenticated: Bool) {
        do {
            if !authenticated {
                session.respond(success(request, try pair(request)))
                return
            }
            guard let fingerprint = session.peer, approved[fingerprint] != nil else {
                session.stop()
                return
            }
            guard request.target == target else {
                throw WireError("wrong_target", "The selected service identity does not match this render host")
            }
            let key = fingerprint + ":" + request.id
            if request.method == "receipt.get" {
                try request.require(["id"])
                guard let query = request.params["id"] as? String, query.utf8.count <= 64 else {
                    throw WireError("invalid_request", "A bounded request ID is required")
                }
                let reference = fingerprint + ":" + query
                let result: WireObject
                if let (_, response) = receipts[reference] {
                    result = ["state": "completed", "response": response]
                } else {
                    result = ["state": pendingRequest == reference ? "pending" : "unknown"]
                }
                session.respond(success(request, result))
                return
            }
            if let (bytes, previous) = receipts[key] {
                guard bytes == request.bytes else { throw WireError("id_conflict", "Request ID has different content") }
                session.respond(previous)
                return
            }
            guard !busy else { throw WireError("busy", "A private renderer operation is already in flight") }
            let retained = mutations.contains(request.method)
            busy = true
            if retained { pendingRequest = key }
            draftQueue.async { [self] in
                let response: WireObject
                do { response = success(request, try draft.execute(request)) }
                catch { response = failure(request, error) }
                queue.async { [self] in
                    busy = false
                    pendingRequest = nil
                    if retained && approved[fingerprint] != nil && bootstrap != nil {
                        if receipts.count >= 64, let oldest = receiptOrder.first {
                            receiptOrder.removeFirst()
                            receipts.removeValue(forKey: oldest)
                        }
                        receiptOrder.append(key)
                        receipts[key] = (request.bytes, response)
                    }
                    if approved[fingerprint] != nil { session.respond(response) } else { session.stop() }
                }
            }
        } catch {
            session.respond(failure(request, error))
        }
    }

    private func pair(_ request: Request) throws -> WireObject {
        if request.method == "hello" {
            try request.require([])
            guard request.target == nil || request.target == target else {
                throw WireError("wrong_target", "Discovered target does not match this render host")
            }
            guard let rpcPort = authenticated?.port?.rawValue else {
                throw WireError("not_ready", "Authenticated draft listener is still starting")
            }
            return ["role": "draft", "target": target, "version": wireVersion, "rpcPort": rpcPort]
        }
        guard request.target == target else {
            throw WireError("wrong_target", "Verify the render host identity before pairing")
        }
        let now = Date()
        pairing = pairing.filter { $0.value.expiration.addingTimeInterval(60) > now }
        switch request.method {
        case "pair.start":
            try request.require(["name", "certificate"])
            guard pairing.count < 8 else { throw WireError("pair_capacity", "Too many pending pairing requests") }
            guard let name = request.params["name"] as? String, (1...80).contains(name.utf8.count),
                  !name.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains),
                  let encoded = request.params["certificate"] as? String, encoded.utf8.count <= 6_000,
                  let bytes = Data(base64Encoded: encoded), (1...4_096).contains(bytes.count),
                  let certificate = SecCertificateCreateWithData(nil, bytes as CFData),
                  SecCertificateCopyKey(certificate) != nil else {
                throw WireError("invalid_pairing", "Expected a controller name and public TLS certificate")
            }
            let id = UUID().uuidString.lowercased()
            let fingerprint = sha256Hex(bytes)
            let expiration = now.addingTimeInterval(90)
            pairing[id] = PairingEntry(request: id, name: name, fingerprint: fingerprint, expiration: expiration)
            queue.asyncAfter(deadline: .now() + 90) { [weak self] in self?.publish() }
            publish()
            return ["request": id, "fingerprint": fingerprint,
                    "expiresAt": Int(expiration.timeIntervalSince1970 * 1000)]
        case "pair.status":
            try request.require(["request"])
            guard let id = request.params["request"] as? String, let entry = pairing[id], entry.expiration > now else {
                return ["state": "expired"]
            }
            if entry.state != "pending" { pairing.removeValue(forKey: id) }
            return ["state": entry.state]
        default:
            throw WireError("unpaired", "Draft operations require an approved client certificate in TLS")
        }
    }

    private func success(_ request: Request, _ result: WireObject) -> WireObject {
        ["id": request.id, "ok": true, "result": result]
    }

    private func failure(_ request: Request, _ error: Error) -> WireObject {
        ["id": request.id, "ok": false, "error": (error as? WireError
            ?? WireError("host_error", "Could not complete the private draft operation")).fields]
    }
}
