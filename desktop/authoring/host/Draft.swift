import Foundation

final class Draft {
    private let target: String
    private let queue: DispatchQueue
    private var renderer: DraftRenderer?
    private var validator: DraftRenderer?
    private var validationIdle: DispatchWorkItem?
    private var epoch = UUID().uuidString.lowercased()
    private var revision = 0
    private var needsRestore = true
    private var activeDigest: String?
    private var slots: [String: WireObject] = [:]
    private var bindings: [String: [WireObject]] = [:]
    private var lastSampleTime: UInt64 = 0
    private var activation: WireObject?
    private struct Candidate {
        let lease: String
        let token: String
        let revision: Int
        let deadline: UInt64
        let digest: String
        let controlDigest: String
        let slots: [String: WireObject]
        let bindings: [String: [WireObject]]
        let attempt: WireObject
    }
    private enum ControlChange {
        case slot(String, WireObject, Double)
        case uniform(String, [WireObject])
    }
    private enum Mutation {
        case run(Candidate)
        case controls(lease: String, revision: Int, change: ControlChange)
    }
    private var candidate: Candidate?
    private var uncertainMutation: (id: String, mutation: Mutation)?
    private var recoveredMutation: (id: String, outcome: Result<WireObject, Error>)?

    // Fence admission runs independently of the normal request queue. The
    // published child is immutable for socket exchanges and scoped by epoch.
    private let fenceLock = NSLock()
    private var fenceEpoch = ""
    private var fenceChild: DraftRenderer?
    private var fenced: Set<String> = []
    var onReset: (() -> Void)?

    init(target: String, queue: DispatchQueue) {
        self.target = target
        self.queue = queue
        fenceEpoch = epoch
    }

    func reset() {
        let child = renderer
        renderer = nil
        epoch = UUID().uuidString.lowercased()
        revision = 0
        fenceLock.withLock {
            fenceEpoch = epoch
            fenceChild = nil
            fenced.removeAll()
        }
        needsRestore = true
        activeDigest = nil
        activation = nil
        candidate = nil
        uncertainMutation = nil
        recoveredMutation = nil
        slots.removeAll()
        bindings.removeAll()
        lastSampleTime = 0
        child?.stop()
        stopValidator()
        onReset?()
    }

    func execute(_ request: Request) throws -> WireObject {
        if let renderer = renderer, !renderer.process.isRunning { reset() }
        switch request.method {
        case "health", "reflect":
            try request.require([])
            return try state()
        case "validate":
            try request.require(["source", "sha256"])
            return try validate(Fragment(request))
        case "run.prepare":
            try settleUnknown()
            try expected(request)
            return try prepare(request)
        case "run.commit":
            try settleUnknown()
            try expected(request)
            return try commit(request)
        case "run.cancel":
            try settleUnknown()
            try request.require(["lease", "token"])
            try expected(request)
            if let staged = candidate, staged.lease == request.params["lease"] as? String,
               staged.token == request.params["token"] as? String {
                try discard(staged)
            }
            return ["target": target, "epoch": epoch, "revision": revision]
        case "slot.configure":
            try requireActive(request)
            return try configureSlot(request)
        case "uniform.set":
            try requireActive(request)
            return try bindUniform(request)
        case "sample":
            try requireActive(request)
            return try relaySample(request)
        default:
            throw WireError("unsupported_method", "This draft operation is unavailable")
        }
    }

    private func expected(_ request: Request) throws {
        guard request.epoch == epoch else {
            throw WireError("stale_epoch", "Renderer process changed. Refresh the selected draft before restoring it")
        }
        guard request.revision == revision else {
            throw WireError("stale_revision", "Draft revision changed. Read back current controls before applying edits")
        }
    }

    func fence(_ request: Request) throws -> WireObject {
        try request.require(["lease"])
        guard let lease = request.params["lease"] as? String else {
            throw WireError("invalid_request", "A preview lease is required")
        }
        let child = try fenceLock.withLock {
            guard request.epoch == fenceEpoch else {
                throw WireError("stale_epoch", "Renderer process changed before the lease was fenced")
            }
            fenced.insert(lease)
            return fenceChild
        }
        if let child = child {
            try fenceNative(child, lease: lease, id: request.id)
        }
        return ["target": target, "epoch": request.epoch ?? "", "lease": lease]
    }

    func revoke(_ lease: String) throws {
        let child = fenceLock.withLock {
            fenced.insert(lease)
            return fenceChild
        }
        if let child = child {
            try fenceNative(child, lease: lease, id: UUID().uuidString)
        }
    }

    private func fenceNative(_ child: DraftRenderer, lease: String, id: String) throws {
        let receipt = try child.mutate("draft_fence", ["lease": lease], id: id, expecting: "ok")
        guard (receipt["result"] as? WireObject)?["lease"] as? String == lease else {
            throw WireError("renderer_protocol", "Native fence returned a different lease")
        }
    }

    private func requireActive(_ request: Request) throws {
        try settleUnknown()
        try expected(request)
        guard let lease = activation?["lease"] as? String, !isFenced(lease) else {
            throw WireError("restore_required", "Run or restore this draft before changing its controls")
        }
    }

    private func isFenced(_ lease: String) -> Bool {
        fenceLock.withLock { fenced.contains(lease) }
    }

    private func discard(_ staged: Candidate) throws {
        candidate = nil
        if let renderer = renderer {
            _ = try renderer.mutate("draft_cancel", ["lease": staged.lease, "token": staged.token], expecting: "ok")
        }
    }

    private func prepare(_ request: Request) throws -> WireObject {
        try request.require(["source", "sha256", "lease", "documentId", "generation", "editSequence",
                             "runSequence", "slots", "bindings"])
        guard let lease = request.params["lease"] as? String, UUID(uuidString: lease) != nil, !isFenced(lease),
              let document = request.params["documentId"] as? String, UUID(uuidString: document) != nil,
              let generation = wireInt(request.params["generation"]), generation >= 0,
              let edit = wireInt(request.params["editSequence"]), edit >= 0,
              let run = wireInt(request.params["runSequence"]), run > 0,
              let requestedSlots = request.params["slots"] as? [WireObject], requestedSlots.count <= 16,
              let intent = request.params["bindings"] as? WireObject, intent.count <= 128 else {
            throw WireError("invalid_request", "Expected bounded draft source and control intent")
        }
        var definitions: [String: WireObject] = [:]
        var defaults: WireObject = [:]
        for raw in requestedSlots {
            let (id, definition) = try slotDefinition(raw)
            guard definitions[id] == nil else { throw WireError("invalid_slot", "Duplicate slot identity") }
            definitions[id] = definition
            defaults[id] = definition["default"]
        }
        let fragment = try Fragment(request)
        if let previous = candidate { try discard(previous) }
        stopValidator()
        return try withRenderer(start: true) { child in
            guard !isFenced(lease) else { throw WireError("stale_lease", "The draft lease was fenced") }
            let baseline = try child.query("get_scene")
            guard let nativeRevision = wireInt(baseline["revision"]) else {
                throw WireError("renderer_protocol", "Native configuration revision is missing")
            }
            let receipt = try child.mutate("draft_prepare", [
                "source_base64": fragment.base64, "source_digest": fragment.digest, "lease": lease, "token": request.id,
                "revision": nativeRevision, "slots": defaults, "bindings": intent,
            ], id: request.id, expecting: "prepared")
            guard let result = receipt["result"] as? WireObject, result["token"] as? String == request.id,
                  let effective = result["bindings"] as? [String: WireObject],
                  let conflicts = result["conflicts"] as? [WireObject] else {
                throw WireError("renderer_protocol", "Native preparation returned an unmatched token")
            }
            var applied: [String: [WireObject]] = [:]
            for (name, binding) in effective {
                guard let components = binding["components"] as? [WireObject] else {
                    throw WireError("renderer_protocol", "Native preparation returned malformed bindings")
                }
                applied[name] = components
            }
            let digest = sha256Hex(try JSONSerialization.data(withJSONObject: ["slots": requestedSlots.sorted {
                ($0["id"] as? String ?? "") < ($1["id"] as? String ?? "")
            }, "bindings": effective], options: [.sortedKeys]))
            let attempt: WireObject = ["documentId": document, "generation": generation,
                                       "editSequence": edit, "runSequence": run]
            let staged = Candidate(lease: lease, token: request.id, revision: nativeRevision,
                                   deadline: DispatchTime.now().uptimeNanoseconds + 20_000_000_000,
                                   digest: fragment.digest, controlDigest: digest, slots: definitions,
                                   bindings: applied, attempt: attempt)
            if isFenced(lease) {
                try discard(staged)
                throw WireError("stale_lease", "The draft lease was fenced")
            }
            candidate = staged
            return attempt.merging([
                "target": target, "epoch": epoch, "revision": revision, "lease": lease, "token": request.id,
                "sourceDigest": fragment.digest, "controlDigest": digest, "bindings": effective, "conflicts": conflicts,
            ]) { old, _ in old }
        }
    }

    private func commit(_ request: Request) throws -> WireObject {
        try request.require(["lease", "token"])
        guard let staged = candidate, staged.lease == request.params["lease"] as? String,
              staged.token == request.params["token"] as? String, !isFenced(staged.lease) else {
            throw WireError("stale_candidate", "Prepare the current source again before applying it")
        }
        guard DispatchTime.now().uptimeNanoseconds < staged.deadline else {
            try discard(staged)
            throw WireError("stale_candidate", "The prepared draft expired")
        }
        candidate = nil
        guard let child = renderer else { throw WireError("restore_required", "The draft renderer is unavailable") }
        return try applyMutation(.run(staged), child: child, method: "draft_commit", params: [
            "lease": staged.lease, "token": staged.token, "revision": staged.revision,
            "control_digest": staged.controlDigest,
        ], id: request.id)
    }

    private func applyMutation(_ mutation: Mutation, child: DraftRenderer, method: String,
                               params: WireObject, id: String) throws -> WireObject {
        do {
            return try confirm(mutation, id: id, receipt: child.mutate(method, params, id: id))
        } catch let error as WireError where isUncertain(error) {
            uncertainMutation = (id, mutation)
            try settleUnknown()
            guard let recovered = recoveredMutation, recovered.id == id else { throw error }
            return try recovered.outcome.get()
        } catch let error as WireError where error.code == "draft_state_lost" {
            reset()
            throw error
        }
    }

    private func isUncertain(_ failure: WireError) -> Bool {
        ["renderer_timeout", "renderer_transport", "renderer_protocol", "request_not_found"].contains(failure.code)
    }

    private func settleUnknown() throws {
        guard let unknown = uncertainMutation else { return }
        guard let child = renderer else { throw WireError("restore_required", "The draft renderer is unavailable") }
        do {
            let receipt = try child.recover(unknown.id, expecting: "rendered")
            let result = try confirm(unknown.mutation, id: unknown.id, receipt: receipt)
            uncertainMutation = nil
            recoveredMutation = (unknown.id, .success(result))
        } catch let error as WireError where isUncertain(error) {
            throw WireError("renderer_uncertain", "The native draw is unconfirmed. Recheck the same host or restart the draft service")
        } catch {
            if (error as? WireError)?.code == "draft_state_lost" { reset() }
            uncertainMutation = nil
            recoveredMutation = (unknown.id, .failure(error))
        }
    }

    func recoverMutation(_ id: String) throws -> WireObject? {
        if uncertainMutation?.id == id { try settleUnknown() }
        return try completedMutation(id)?.get()
    }

    func completedMutation(_ id: String) -> Result<WireObject, Error>? {
        guard let terminal = recoveredMutation, terminal.id == id else { return nil }
        return terminal.outcome
    }

    private func confirm(_ mutation: Mutation, id: String, receipt: WireObject) throws -> WireObject {
        switch mutation {
        case .run(let staged):
            return try confirmRun(staged, id: id, receipt: receipt)
        case .controls(let lease, let previousRevision, let change):
            return try confirmControls(change, lease: lease, previousRevision: previousRevision, receipt: receipt)
        }
    }

    private func confirmRun(_ staged: Candidate, id: String, receipt: WireObject) throws -> WireObject {
        guard let result = receipt["result"] as? WireObject, result["lease"] as? String == staged.lease,
              result["token"] as? String == staged.token, result["source_digest"] as? String == staged.digest,
              result["control_digest"] as? String == staged.controlDigest, result["commit_id"] as? String == id,
              let scene = result["scene"] as? WireObject, let nativeRevision = wireInt(receipt["revision"]),
              nativeRevision > staged.revision, wireInt(scene["rendered_revision"]) == nativeRevision,
              wireInt(scene["frame"]) == wireInt(receipt["frame"]) else {
            throw WireError("renderer_protocol", "The native draw did not confirm the prepared draft")
        }
        revision = nativeRevision
        slots = staged.slots
        bindings = staged.bindings
        activeDigest = staged.digest
        needsRestore = false
        activation = staged.attempt.merging(["lease": staged.lease, "token": staged.token,
                                             "controlDigest": staged.controlDigest, "commitID": id]) { old, _ in old }
        lastSampleTime = 0
        return snapshot(scene, receipt: receipt)
    }

    private func stopValidator() {
        validationIdle?.cancel()
        validationIdle = nil
        validator?.stop()
        validator = nil
    }

    private func validate(_ fragment: Fragment) throws -> WireObject {
        validationIdle?.cancel()
        if validator?.process.isRunning != true { validator = try DraftRenderer(validation: true) }
        guard let child = validator else { throw WireError("renderer_startup", "No owned validator available") }
        defer {
            if validator != nil {
                let idle = DispatchWorkItem { [weak self] in self?.stopValidator() }
                validationIdle = idle
                queue.asyncAfter(deadline: .now() + 2, execute: idle)
            }
        }
        do {
            let receipt = try child.mutate("publish_shader", ["layer": 0, "source_base64": fragment.base64])
            let scene = try correlatedScene(child, receipt)
            return ["target": target, "epoch": epoch, "sourceDigest": fragment.digest, "receipt": receipt,
                    "shader": ["uniforms": uniforms(scene)]]
        } catch let error as WireError {
            if error.code != "shader_failed" { stopValidator() }
            throw error
        }
    }

    private func slotID(_ raw: Any?) -> String? {
        guard let id = raw as? String, (1...32).contains(id.utf8.count),
              id.range(of: "^[a-zA-Z][a-zA-Z0-9_]*$", options: .regularExpression) != nil else { return nil }
        return id
    }

    private func slotDefinition(_ raw: WireObject) throws -> (String, WireObject) {
        guard Set(raw.keys) == ["id", "label", "default"], let id = slotID(raw["id"]),
              let label = raw["label"] as? String, (1...80).contains(label.utf8.count),
              !label.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              !label.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains),
              let initial = wireNumber(raw["default"]), (0...1).contains(initial) else {
            throw WireError("invalid_slot", "Expected a bounded slot ID, display label, and normalized default")
        }
        return (id, ["id": id, "label": label, "default": initial])
    }

    private func configureSlot(_ request: Request) throws -> WireObject {
        try request.require(["id", "label", "default"])
        let (id, definition) = try slotDefinition(request.params)
        guard let initial = wireNumber(definition["default"]) else {
            throw WireError("invalid_slot", "The configured slot has no normalized default")
        }
        guard slots.count < 16 || slots[id] != nil else { throw WireError("slot_capacity", "Draft has 16 slots") }
        return try applyControls(.slot(id, definition, initial), request: request, method: "draft_configure_slot",
                                 params: ["slot": id, "initial": initial])
    }

    private func bindingComponent(_ request: Request) throws -> WireObject {
        switch request.params["mode"] as? String {
        case "fixed":
            try request.require(["name", "component", "mode", "value"])
            guard let value = wireNumber(request.params["value"]), abs(value) <= 1_000_000 else {
                throw WireError("invalid_uniform", "A finite bounded uniform value is required")
            }
            return ["fixed": value]
        case "range":
            try request.require(["name", "component", "mode", "slot", "low", "high"])
            guard let id = slotID(request.params["slot"]), slots[id] != nil else {
                throw WireError("missing_slot", "Configure the mapped slot before applying a range")
            }
            guard let low = wireNumber(request.params["low"]), let high = wireNumber(request.params["high"]),
                  abs(low) <= 1_000_000, abs(high) <= 1_000_000 else {
                throw WireError("invalid_uniform", "Finite bounded range endpoints are required")
            }
            return ["slot": id, "low": low, "high": high]
        default:
            throw WireError("invalid_uniform", "Choose fixed or ranged binding")
        }
    }

    private func bindUniform(_ request: Request) throws -> WireObject {
        let component = try bindingComponent(request)
        guard let name = request.params["name"] as? String, (1...128).contains(name.utf8.count),
              let index = wireInt(request.params["component"]), index >= 0 else {
            throw WireError("invalid_uniform", "Expected a writable uniform and component index")
        }
        return try withRenderer { child in
            let live = try child.query("get_scene")
            guard let reflection = uniforms(live).first(where: { $0["name"] as? String == name }),
                  reflection["writable"] as? Bool == true,
                  let values = reflection["values"] as? [Double], values.indices.contains(index) else {
                throw WireError("invalid_uniform", "Uniform is unavailable, renderer-owned, or component is out of range")
            }
            var components = bindings[name] ?? values.map { ["fixed": $0] }
            guard components.count == values.count else {
                throw WireError("renderer_protocol", "Native reflection changed the uniform arity")
            }
            components[index] = component
            return try applyControls(.uniform(name, components), request: request, method: "draft_bind_uniform",
                                     params: ["layer": 0, "name": name, "components": components])
        }
    }

    private func applyControls(_ change: ControlChange, request: Request, method: String,
                               params: WireObject) throws -> WireObject {
        guard let lease = activation?["lease"] as? String, !isFenced(lease) else {
            throw WireError("stale_lease", "The draft lease was fenced before applying the controls")
        }
        return try withRenderer { child in
            try applyMutation(.controls(lease: lease, revision: revision, change: change), child: child,
                              method: method, params: params.merging(["lease": lease, "revision": revision]) { first, _ in first },
                              id: request.id)
        }
    }

    private func confirmControls(_ change: ControlChange, lease: String, previousRevision: Int,
                                 receipt: WireObject) throws -> WireObject {
        guard let result = receipt["result"] as? WireObject, result["lease"] as? String == lease,
              let scene = result["scene"] as? WireObject,
              let newRevision = wireInt(receipt["revision"]), newRevision > previousRevision,
              wireInt(scene["rendered_revision"]) == newRevision,
              wireInt(scene["frame"]) == wireInt(receipt["frame"]),
              (scene["draft_activation"] as? WireObject)?["lease"] as? String == lease else {
            throw WireError("renderer_protocol", "Native draw did not confirm the requested control change")
        }
        let samples = scene["draft_slots"] as? WireObject ?? [:]
        switch change {
        case let .slot(id, definition, initial):
            guard let observed = wireNumber(samples[id]), abs(observed - Double(Float(initial))) < 1e-6 else {
                throw WireError("renderer_protocol", "Native renderer did not configure the requested slot")
            }
            slots[id] = definition
        case let .uniform(name, components):
            guard let returned = uniforms(scene).first(where: { $0["name"] as? String == name })?["values"] as? [Double],
                  returned.count == components.count else {
                throw WireError("renderer_protocol", "Native readback did not return the edited uniform")
            }
            for (part, actual) in zip(components, returned) {
                if let fixed = wireNumber(part["fixed"]) {
                    let expected = Double(Float(fixed))
                    guard abs(actual - expected) <= max(1e-6, abs(expected) * 0.000001) else {
                        throw WireError("renderer_protocol", "Drawn fixed uniform does not match its binding")
                    }
                } else {
                    // Native evaluation and readback use the shipped float expression parser.
                    guard actual.isFinite, let slot = part["slot"] as? String, wireNumber(samples[slot]) != nil else {
                        throw WireError("renderer_protocol", "Mapped native slot was not reflected")
                    }
                }
            }
            bindings[name] = components
        }
        candidate = nil
        revision = newRevision
        return snapshot(scene, receipt: receipt)
    }

    private func relaySample(_ request: Request) throws -> WireObject {
        try request.require(["lease", "sequence", "slot", "value"])
        guard let lease = request.params["lease"] as? String, lease == activation?["lease"] as? String,
              let sequence = wireInt(request.params["sequence"]), sequence > 0,
              let slot = slotID(request.params["slot"]), slots[slot] != nil,
              let amplitude = wireNumber(request.params["value"]), (0...1).contains(amplitude) else {
            throw WireError("invalid_sample", "Expected a sample for the current draft lease and slot")
        }
        let now = DispatchTime.now().uptimeNanoseconds
        guard now - lastSampleTime >= 15_000_000 else {
            throw WireError("sample_rate", "Sample relay rate exceeded")
        }
        lastSampleTime = now
        return try withRenderer { child in
            let receipt = try child.mutate("draft_sample", ["lease": lease, "revision": revision,
                "sequence": sequence, "slot": slot, "value": amplitude], id: request.id)
            guard let result = receipt["result"] as? WireObject, result["lease"] as? String == lease,
                  wireInt(result["sequence"]) == sequence, let scene = result["scene"] as? WireObject,
                  wireInt(scene["frame"]) == wireInt(receipt["frame"]),
                  wireInt(receipt["revision"]) == revision else {
                throw WireError("sample_unobserved", "The native renderer did not draw the tagged sample")
            }
            return snapshot(scene)
        }
    }

    private func withRenderer(start: Bool = false, _ operation: (DraftRenderer) throws -> WireObject) throws -> WireObject {
        if start && renderer == nil {
            let child = try DraftRenderer(validation: false)
            renderer = child
            fenceLock.withLock { fenceChild = child }
            child.process.terminationHandler = { [weak self, weak child] _ in
                guard let self = self, let child = child else { return }
                self.queue.async { if self.renderer === child { self.reset() } }
            }
        }
        guard let child = renderer else { throw WireError("restore_required", "Run or restore a saved shader first") }
        return try operation(child)
    }

    private func correlatedScene(_ child: DraftRenderer, _ receipt: WireObject) throws -> WireObject {
        let scene = try child.query("get_scene")
        guard wireInt(scene["rendered_revision"]) == wireInt(receipt["revision"]),
              wireInt(scene["external_epoch"]) == wireInt(receipt["external_epoch"]),
              let frame = wireInt(scene["frame"]), let drawn = wireInt(receipt["frame"]), frame >= drawn,
              let layer = (scene["layers"] as? [WireObject])?.first, let shader = layer["shader"] as? WireObject,
              shader["loaded"] as? Bool == true, (wireInt(shader["program"]) ?? 0) > 0 else {
            throw WireError("renderer_protocol", "Drawn receipt does not match the active native shader")
        }
        return scene
    }

    private func uniforms(_ scene: WireObject) -> [WireObject] {
        guard let layer = (scene["layers"] as? [WireObject])?.first,
              let shader = layer["shader"] as? WireObject,
              let declarations = shader["uniforms"] as? WireObject else { return [] }
        return declarations.keys.sorted().compactMap { name in
            guard let declaration = declarations[name] as? WireObject,
                  let glType = wireInt(declaration["gl_type"]) else { return nil }
            let types = [0x1406: "float", 0x8b50: "vec2", 0x8b51: "vec3", 0x8b52: "vec4"]
            let writable = types[glType] != nil && declaration["writable"] as? Bool == true
            var observation = declaration["rendered_value"]
            if let raw = observation as? String {
                observation = try? JSONSerialization.jsonObject(with: Data(raw.utf8), options: [.fragmentsAllowed])
            }
            let values = (observation as? [Any] ?? [observation ?? NSNull()]).compactMap(wireNumber)
            var uniform: WireObject = ["name": name, "owner": "layer", "writable": writable,
                                       "type": types[glType] ?? "0x\(String(glType, radix: 16))", "values": values]
            if let binding = declaration["binding"] as? String { uniform["binding"] = binding }
            return uniform
        }
    }

    private func state() throws -> WireObject {
        try settleUnknown()
        let current = try renderer?.query("get_scene") ?? [:]
        return snapshot(current)
    }

    private func snapshot(_ scene: WireObject, receipt: WireObject? = nil) -> WireObject {
        var result: WireObject = ["role": "draft", "version": wireVersion, "target": target, "epoch": epoch,
                                  "revision": revision, "uniforms": uniforms(scene),
                                  "slots": slots.keys.sorted().compactMap { slots[$0] }, "needsRestore": needsRestore]
        if let frame = wireInt(scene["frame"]) { result["frame"] = frame }
        if let samples = scene["draft_slots"] as? WireObject { result["samples"] = samples }
        if let receipt = receipt { result["receipt"] = receipt }
        if let digest = activeDigest { result["sourceDigest"] = digest }
        if let active = activation { result.merge(active) { old, _ in old } }
        return result
    }
}
