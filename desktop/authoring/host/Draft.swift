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
    var onReset: (() -> Void)?

    init(target: String, queue: DispatchQueue) {
        self.target = target
        self.queue = queue
    }

    func reset() {
        let child = renderer
        renderer = nil
        epoch = UUID().uuidString.lowercased()
        revision = 0
        needsRestore = true
        activeDigest = nil
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
            do { return try state() }
            catch {
                reset()
                return try state()
            }
        case "validate":
            let fragment = try Fragment(request)
            return try validate(fragment)
        case "activate":
            try expected(request)
            let fragment = try Fragment(request)
            stopValidator()
            return try withRenderer(start: true) { child in
                let receipt = try child.mutate("draft_activate", ["layer": 0, "source": fragment.text])
                let scene = try correlatedScene(child, receipt)
                revision = wireInt(receipt["revision"]) ?? revision
                activeDigest = fragment.digest
                needsRestore = false
                slots.removeAll()
                bindings.removeAll()
                return snapshot(scene, receipt: receipt)
            }
        case "slot.configure":
            try expected(request)
            return try configureSlot(request)
        case "uniform.set":
            try expected(request)
            return try bindUniform(request)
        case "sample":
            try expected(request)
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
            let receipt = try child.mutate("publish_shader", ["layer": 0, "source": fragment.text])
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

    private func configureSlot(_ request: Request) throws -> WireObject {
        try request.require(["id", "label", "default"])
        guard let id = slotID(request.params["id"]), let label = request.params["label"] as? String,
              (1...80).contains(label.utf8.count), !label.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              !label.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains),
              let initial = wireNumber(request.params["default"]), (0...1).contains(initial) else {
            throw WireError("invalid_slot", "Expected a bounded slot ID, a display label, and a normalized default")
        }
        guard slots.count < 16 || slots[id] != nil else { throw WireError("slot_capacity", "Draft has 16 slots") }
        return try withRenderer { child in
            let receipt = try child.mutate("draft_configure_slot", ["slot": id, "initial": initial])
            let scene = try correlatedScene(child, receipt)
            let samples = scene["draft_slots"] as? WireObject
            guard let observed = wireNumber(samples?[id]), abs(observed - Double(Float(initial))) < 1e-6 else {
                throw WireError("renderer_protocol", "Native renderer did not configure the requested slot")
            }
            slots[id] = ["id": id, "label": label, "default": initial]
            revision = wireInt(receipt["revision"]) ?? revision
            return snapshot(scene, receipt: receipt)
        }
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
            let receipt = try child.mutate("draft_bind_uniform", ["layer": 0, "name": name, "components": components])
            let scene = try correlatedScene(child, receipt)
            guard let returned = uniforms(scene).first(where: { $0["name"] as? String == name })?["values"] as? [Double],
                  returned.count == components.count else {
                throw WireError("renderer_protocol", "Native readback did not return the edited uniform")
            }
            let observed = scene["draft_slots"] as? WireObject ?? [:]
            for (part, actual) in zip(components, returned) {
                let expected: Double
                if let fixed = wireNumber(part["fixed"]) {
                    expected = Double(Float(fixed))
                } else if let slot = part["slot"] as? String, let amplitude = wireNumber(observed[slot]),
                          let low = wireNumber(part["low"]), let high = wireNumber(part["high"]) {
                    expected = Double(Float(low) + Float(high - low) * Float(amplitude))
                } else {
                    throw WireError("renderer_protocol", "Mapped native slot was not reflected")
                }
                guard abs(actual - expected) <= max(1e-6, abs(expected) * 0.000001) else {
                    throw WireError("renderer_protocol", "Drawn uniform does not match its composed binding")
                }
            }
            bindings[name] = components
            revision = wireInt(receipt["revision"]) ?? revision
            return snapshot(scene, receipt: receipt)
        }
    }

    private func relaySample(_ request: Request) throws -> WireObject {
        try request.require(["slot", "value"])
        guard let slot = slotID(request.params["slot"]), slots[slot] != nil else {
            throw WireError("missing_slot", "The sample slot is not configured in the selected draft")
        }
        guard let amplitude = wireNumber(request.params["value"]), (0...1).contains(amplitude) else {
            throw WireError("invalid_sample", "Expected a normalized OSC amplitude")
        }
        let now = DispatchTime.now().uptimeNanoseconds
        guard now - lastSampleTime >= 15_000_000 else {
            throw WireError("sample_rate", "Sample relay rate exceeded")
        }
        lastSampleTime = now
        return try withRenderer { child in
            let until = DispatchTime.now().uptimeNanoseconds + 800_000_000
            let first = try child.query("get_scene", until: until)
            try child.sample(slot: slot, amplitude: amplitude)
            let previousFrame = wireInt(first["frame"]) ?? 0
            let previousExternal = wireInt(first["external_epoch"]) ?? 0
            var observedAt: Int?
            while DispatchTime.now().uptimeNanoseconds < until {
                let result = try child.query("get_scene", until: until)
                let values = result["draft_slots"] as? WireObject
                if let frame = wireInt(result["frame"]), frame > previousFrame,
                   (wireInt(result["external_epoch"]) ?? 0) > previousExternal,
                   let observed = wireNumber(values?[slot]), abs(observed - Double(Float(amplitude))) < 1e-6 {
                    // Input dispatch precedes composition. A sample seen by
                    // get_scene can still have the preceding GPU readback.
                    if let admitted = observedAt, frame > admitted { return snapshot(result) }
                    if observedAt == nil { observedAt = frame }
                } else {
                    observedAt = nil
                }
                Thread.sleep(forTimeInterval: 0.01)
            }
            throw WireError("sample_unobserved", "The native renderer did not draw the relayed sample")
        }
    }

    private func withRenderer(start: Bool = false, _ operation: (DraftRenderer) throws -> WireObject) throws -> WireObject {
        do {
            if start && renderer == nil {
                let child = try DraftRenderer(validation: false)
                renderer = child
                child.process.terminationHandler = { [weak self, weak child] _ in
                    guard let self = self, let child = child else { return }
                    self.queue.async { if self.renderer === child { self.reset() } }
                }
            }
            guard let child = renderer else { throw WireError("restore_required", "Activate a saved or staged shader first") }
            return try operation(child)
        } catch let failure as WireError {
            if ["renderer_timeout", "renderer_protocol", "renderer_transport", "renderer_startup"].contains(failure.code) {
                reset()
            }
            throw failure
        }
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
        return result
    }
}
