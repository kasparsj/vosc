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
    private var pendingRequests: [String: Data] = [:]
    private enum DrawKind { case run, controls }
    private struct UnconfirmedDraw {
        let key: String
        let id: String
        let owner: String
        let bytes: Data
        let kind: DrawKind
    }
    private var uncertainRequest: UnconfirmedDraw?
    private var leaseOwner: String?
    private var previewLease: String?
    private let fenceQueue = DispatchQueue(label: "com.vosc.render-host.draft-fence")
    private var fencesInFlight = 0
    private var revokedPreviewLease: String?
    private var receipts: [String: (Data, WireObject)] = [:]
    private var receiptOrder: [String] = []
    private var lastDraws: [DrawKind: (key: String, bytes: Data, response: WireObject)] = [:]
    private let mutations: Set<String> = [
        "run.prepare", "run.commit", "run.cancel", "run.fence", "slot.configure", "uniform.set",
    ]
    private let controlled: Set<String> = ["run.commit", "run.cancel", "run.fence", "slot.configure", "uniform.set", "sample"]

    init(updated: @escaping (HostView) -> Void) {
        let previous = preferences.string(forKey: "draft-host-target")
        target = previous.flatMap { UUID(uuidString: $0)?.uuidString.lowercased() } ?? UUID().uuidString.lowercased()
        preferences.set(target, forKey: "draft-host-target")
        approved = preferences.dictionary(forKey: "draft-host-approved") as? [String: String] ?? [:]
        draft = Draft(target: target, queue: draftQueue)
        self.updated = updated
        draft.onReset = { [weak self] in
            self?.queue.async { [weak self] in
                guard let self = self else { return }
                let revokedOutcome = self.uncertainRequest.map { self.approved[$0.owner] == nil } ?? false
                let revocationPending = self.revokedPreviewLease != nil || revokedOutcome
                self.receipts.removeAll()
                self.receiptOrder.removeAll()
                self.lastDraws.removeAll()
                self.uncertainRequest = nil
                self.leaseOwner = nil
                self.previewLease = nil
                self.revokedPreviewLease = nil
                if revocationPending, self.bootstrap?.port != nil, self.authenticated?.port != nil {
                    self.status = "Draft service is ready"
                    self.publish()
                }
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
            lastDraws = lastDraws.filter { !$0.value.key.hasPrefix(prefix) }
            if leaseOwner == fingerprint, let lease = previewLease, revokedPreviewLease != lease {
                revokedPreviewLease = lease
                fencesInFlight += 1
                fenceQueue.async { [self] in
                    let outcome = Result { try draft.revoke(lease) }
                    queue.async { [self] in
                        fencesInFlight -= 1
                        guard previewLease == lease, leaseOwner == fingerprint else { return }
                        switch outcome {
                        case .success:
                            leaseOwner = nil
                            previewLease = nil
                            revokedPreviewLease = nil
                            if uncertainRequest?.owner == fingerprint { showUnconfirmedRevocation() }
                        case .failure:
                            status = "Controller revoked. Stop and restart the draft service to recover its private preview."
                            publish()
                        }
                    }
                }
            }
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
        lastDraws.removeAll()
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
                if let (_, response) = recordedReceipt(reference) {
                    session.respond(success(request, ["state": "completed", "response": response]))
                } else if pendingRequests[reference] != nil || (uncertainRequest?.key == reference && busy) {
                    session.respond(success(request, ["state": "pending"]))
                } else if uncertainRequest?.key == reference {
                    busy = true
                    draftQueue.async { [self] in
                        let result: WireObject
                        do {
                            if let recovered = try draft.recoverMutation(query) {
                                result = ["state": "completed", "response": ["id": query, "ok": true, "result": recovered]]
                            } else {
                                result = ["state": "unknown"]
                            }
                        } catch let error as WireError where error.code == "renderer_uncertain" {
                            result = ["state": "pending"]
                        } catch {
                            result = ["state": "completed", "response": ["id": query, "ok": false, "error":
                                (error as? WireError ?? WireError("host_error", "Could not recover the native draw")).fields]]
                        }
                        queue.async { [self] in
                            busy = false
                            if let recovered = result["response"] as? WireObject,
                               let uncertain = uncertainRequest, uncertain.key == reference {
                                retireUncertain(uncertain, response: recovered)
                            }
                            if approved[fingerprint] != nil { session.respond(success(request, result)) }
                            else { session.stop() }
                        }
                    }
                } else {
                    session.respond(success(request, ["state": "unknown"]))
                }
                return
            }
            if let (bytes, previous) = recordedReceipt(key) {
                guard bytes == request.bytes else { throw WireError("id_conflict", "Request ID has different content") }
                session.respond(previous)
                return
            }
            if let bytes = pendingRequests[key] {
                guard bytes == request.bytes else { throw WireError("id_conflict", "Request ID has different content") }
                throw WireError("busy", "The same request is already in flight. Check its receipt")
            }
            if let uncertain = uncertainRequest, uncertain.key == key {
                guard uncertain.bytes == request.bytes else { throw WireError("id_conflict", "Request ID has different content") }
                throw WireError("renderer_uncertain", "Check the receipt for the earlier request before retrying")
            }
            let isFence = request.method == "run.fence"
            guard fencesInFlight == 0, isFence || !busy else {
                throw WireError("busy", "A private renderer operation is already in flight")
            }
            if revokedPreviewLease != nil, mutations.contains(request.method) || request.method == "sample" {
                throw WireError("revoked_lease_unconfirmed", "Restart the draft service before publishing or relaying samples")
            }
            if uncertainRequest != nil, !isFence, mutations.contains(request.method) || request.method == "sample" {
                throw WireError("prior_draw_unconfirmed", "Verify the earlier native receipt before publishing or relaying samples")
            }
            let lease = request.params["lease"] as? String
            let previousLease = previewLease
            if request.method == "run.prepare" {
                guard let lease = lease, UUID(uuidString: lease) != nil else {
                    throw WireError("invalid_request", "A preview lease is required")
                }
                guard previewLease == nil || (leaseOwner == fingerprint && previewLease == lease) else {
                    throw WireError("lease_owned", "The current preview lease must be fenced before switching drafts")
                }
                leaseOwner = fingerprint
                previewLease = lease
            } else if controlled.contains(request.method) {
                guard leaseOwner == fingerprint, previewLease != nil,
                      (!["run.commit", "run.cancel", "run.fence", "sample"].contains(request.method) || lease == previewLease) else {
                    throw WireError("stale_lease", "This paired controller does not own the requested preview lease")
                }
            }
            let retained = mutations.contains(request.method)
            let draw = drawKind(request.method)
            if isFence { fencesInFlight += 1 } else { busy = true }
            if retained { pendingRequests[key] = request.bytes }
            let operationQueue = isFence ? fenceQueue : draftQueue
            let previousUncertain = isFence ? nil : uncertainRequest
            operationQueue.async { [self] in
                let response: WireObject
                let uncertain: Bool
                do {
                    let result = try isFence ? draft.fence(request) : draft.execute(request)
                    response = success(request, result)
                    uncertain = false
                } catch {
                    response = failure(request, error)
                    uncertain = (error as? WireError)?.code == "renderer_uncertain"
                }
                let previousOutcome = previousUncertain.flatMap { draft.completedMutation($0.id) }
                queue.async { [self] in
                    if let previous = previousUncertain, let outcome = previousOutcome {
                        retireUncertain(previous, response: drawResponse(previous.id, outcome))
                    }
                    let accepted = response["ok"] as? Bool == true
                    if isFence { fencesInFlight -= 1 } else { busy = false }
                    pendingRequests.removeValue(forKey: key)
                    let failedInitialPrepare = request.method == "run.prepare" && !accepted &&
                        previousLease == nil && revokedPreviewLease != lease
                    if isFence && accepted || failedInitialPrepare,
                       leaseOwner == fingerprint, previewLease == lease {
                        if revokedPreviewLease == lease { revokedPreviewLease = nil }
                        leaseOwner = nil
                        previewLease = nil
                    }
                    if uncertain, let draw = draw {
                        uncertainRequest = UnconfirmedDraw(key: key, id: request.id, owner: fingerprint,
                                                           bytes: request.bytes, kind: draw)
                        if approved[fingerprint] == nil { showUnconfirmedRevocation() }
                    }
                    if retained && !uncertain && approved[fingerprint] != nil && bootstrap != nil {
                        remember(key, bytes: request.bytes, response: response, draw: draw)
                    }
                    if approved[fingerprint] != nil { session.respond(response) } else { session.stop() }
                }
            }
        } catch {
            session.respond(failure(request, error))
        }
    }

    private func drawResponse(_ id: String, _ outcome: Result<WireObject, Error>) -> WireObject {
        switch outcome {
        case .success(let result): return ["id": id, "ok": true, "result": result]
        case .failure(let error):
            let failure = error as? WireError ?? WireError("host_error", "Could not recover the native draw")
            return ["id": id, "ok": false, "error": failure.fields]
        }
    }

    private func retireUncertain(_ request: UnconfirmedDraw, response: WireObject) {
        guard uncertainRequest?.key == request.key else { return }
        uncertainRequest = nil
        if approved[request.owner] != nil, bootstrap != nil {
            remember(request.key, bytes: request.bytes, response: response, draw: request.kind)
        } else if revokedPreviewLease == nil, bootstrap?.port != nil, authenticated?.port != nil {
            status = "Draft service is ready"
            publish()
        }
    }

    private func showUnconfirmedRevocation() {
        status = "Controller revoked. Check the native outcome or stop and restart the draft service."
        publish()
    }

    private func drawKind(_ method: String) -> DrawKind? {
        switch method {
        case "run.commit": return .run
        case "slot.configure", "uniform.set": return .controls
        default: return nil
        }
    }

    private func recordedReceipt(_ key: String) -> (Data, WireObject)? {
        if let current = receipts[key] { return current }
        guard let retained = lastDraws.values.first(where: { $0.key == key }) else { return nil }
        return (retained.bytes, retained.response)
    }

    private func remember(_ key: String, bytes: Data, response: WireObject, draw: DrawKind?) {
        if let draw = draw, response["ok"] as? Bool == true { lastDraws[draw] = (key, bytes, response) }
        if receipts.count >= 64, let oldest = receiptOrder.first {
            receiptOrder.removeFirst()
            receipts.removeValue(forKey: oldest)
        }
        receiptOrder.append(key)
        receipts[key] = (bytes, response)
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
