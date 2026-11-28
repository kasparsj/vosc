import Foundation

func require(_ value: Bool, _ message: String) throws {
    if !value { throw WireError("verification_failed", message) }
}

func rejects(_ code: String, _ operation: () throws -> WireObject) throws {
    do {
        _ = try operation()
        throw WireError("verification_failed", "Expected \(code)")
    } catch let error as WireError where error.code == code {}
}

func verify() throws {
    guard (3...4).contains(CommandLine.arguments.count) else {
        throw WireError("verification_failed", "Usage: verify-webgpu-m2a <fixture directory> <evidence.json> [--recovery]")
    }
    let fixtures = URL(fileURLWithPath: CommandLine.arguments[1])
    let quadrants = try Data(contentsOf: fixtures.appendingPathComponent("m1-quadrants.wgsl"))
    let uniforms = try Data(contentsOf: fixtures.appendingPathComponent("m2a-uniforms.wgsl"))
    let glsl = Data("#version 150\nout vec4 color; void main() { color = vec4(0.1, 0.2, 0.3, 1); }\n".utf8)
    let invalid = Data("@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {\n    return unknown;\n}\n".utf8)
    let lease = UUID().uuidString.lowercased()
    let documentID = UUID().uuidString.lowercased()
    let controlDigest = String(repeating: "a", count: 64)
    let renderer = try DraftRenderer(validation: false)
    defer { renderer.stop() }
    func status() throws -> WireObject { try renderer.query("draft_m1_status") }

    func prepare(_ bytes: Data, _ revision: Int, _ token: String, language: String? = "wgsl",
                 owner: String? = nil) throws -> WireObject {
        var params: WireObject = ["source_base64": bytes.base64EncodedString(), "source_digest": sha256Hex(bytes),
                                  "lease": owner ?? lease, "token": token, "revision": revision, "slots": [:], "bindings": [:]]
        if let language = language { params["language"] = language }
        return try renderer.mutate("draft_prepare", params, id: token, expecting: "prepared")
    }
    func commit(_ revision: Int, _ token: String, owner: String? = nil) throws -> WireObject {
        try renderer.mutate("draft_commit", ["lease": owner ?? lease, "token": token, "revision": revision,
                                             "control_digest": controlDigest], id: "commit-" + token)
    }
    func check(_ receipt: WireObject, _ source: Data, _ token: String, _ revision: Int,
               _ expected: [[Int]]) throws -> [[Int]] {
        let result = receipt["result"] as? WireObject ?? [:]
        let scene = result["scene"] as? WireObject ?? [:]
        try require(wireInt(receipt["revision"]) == revision &&
            wireInt(receipt["frame"]) == wireInt(scene["frame"]) &&
            wireInt(receipt["frame"]) == wireInt(result["first_frame"]) &&
            wireInt(scene["rendered_revision"]) == revision && result["backend"] as? String == "webgpu" &&
            scene["backend"] as? String == "webgpu", "First-frame receipt does not match WebGPU state")
        try require(result["lease"] as? String == lease && result["token"] as? String == token &&
            result["source_digest"] as? String == sha256Hex(source) &&
            result["control_digest"] as? String == controlDigest &&
            result["commit_id"] as? String == receipt["id"] as? String, "First-frame attempt did not match")
        let rgba = (result["pixels"] as? [WireObject] ?? []).compactMap { $0["rgba"] as? [Int] }
        try require(rgba.map { Array($0.prefix(expected[0].count)) } == expected, "Unexpected pixels \(rgba)")
        return rgba
    }
    func retained(_ revision: Int, _ token: String, after previous: Int) throws -> WireObject {
        let live = try renderer.query("get_scene")
        try require(wireInt(live["revision"]) == revision &&
            (live["draft_activation"] as? WireObject)?["token"] as? String == token &&
            (live["layers"] as? [WireObject])?.isEmpty == true, "Accepted WebGPU revision changed")
        for _ in 0..<25 {
            let next = try status()
            if (wireInt(next["webgpu_frames"]) ?? 0) > previous { return next }
            Thread.sleep(forTimeInterval: 0.035)
        }
        throw WireError("verification_failed", "Retained WebGPU pipeline did not continue presenting")
    }

    let initial = try renderer.query("get_scene")
    let base = wireInt(initial["revision"]) ?? -1
    _ = try prepare(glsl, base, "glsl", language: nil)
    let gl = try commit(base, "glsl")
    let glRevision = base + 1
    var evidence: WireObject = ["gl": gl]

    if renderer.supportsDraftWgsl {
        let glStatus = try status()
        try rejects("shader_failed") { try prepare(invalid, glRevision, "invalid-before") }
        let before = try status()
        try require(before["opengl_visible"] as? Bool == true, "Failed prepare hid OpenGL")
        _ = try prepare(quadrants, glRevision, "quadrants")
        let first = try commit(glRevision, "quadrants")
        let firstRevision = glRevision + 1
        _ = try check(first, quadrants, "quadrants", firstRevision,
                      [[255, 64, 0, 255], [0, 128, 255, 255], [191, 0, 128, 255], [0, 255, 64, 255]])
        let firstStatus = try status()
        try require(wireInt(firstStatus["pid"]) == wireInt(glStatus["pid"]) && firstStatus["backend"] as? String == "Metal" &&
            firstStatus["webgpu_visible"] as? Bool == true && firstStatus["opengl_visible"] as? Bool == false,
            "The private renderer did not hand off to Metal")

        try rejects("shader_failed") { try prepare(invalid, firstRevision, "invalid-active") }
        try rejects("backend_switch_required") { try prepare(glsl, firstRevision, "glsl-after", language: "glsl") }
        _ = try prepare(uniforms, firstRevision, "cancelled")
        _ = try renderer.mutate("draft_cancel", ["lease": lease, "token": "cancelled"], expecting: "ok")
        try rejects("stale_candidate") { try commit(firstRevision, "cancelled") }
        let fenced = "revoked-lease"
        _ = try prepare(uniforms, firstRevision, "fenced", owner: fenced)
        _ = try renderer.mutate("draft_fence", ["lease": fenced], expecting: "ok")
        try rejects("stale_lease") { try commit(firstRevision, "fenced", owner: fenced) }
        let afterFailure = try retained(firstRevision, "quadrants", after: wireInt(firstStatus["webgpu_frames"]) ?? 0)

        let clock = Data(String(decoding: uniforms, as: UTF8.self)
            .replacingOccurrences(of: "renderer.time, 0.0, 1.0", with: "renderer.time / 100.0, 0.0, 1.0").utf8)
        _ = try prepare(quadrants, firstRevision, "superseded")
        _ = try prepare(clock, firstRevision, "clock-before")
        try rejects("stale_candidate") { try commit(firstRevision, "superseded") }
        let replacement = try commit(firstRevision, "clock-before")
        let rgb = [[0, 0, 255], [255, 0, 255], [0, 255, 255], [255, 255, 255]]
        let startTime = try check(replacement, clock, "clock-before", firstRevision + 1, rgb)[0][3]
        let replacementStatus = try status()
        try require(wireInt(replacementStatus["pid"]) == wireInt(firstStatus["pid"]) &&
            (wireInt(firstStatus["webgpu_context"]) ?? 0) > 0 &&
            wireInt(replacementStatus["webgpu_context"]) == wireInt(firstStatus["webgpu_context"]) &&
            (wireInt(replacementStatus["webgpu_frames"]) ?? 0) > (wireInt(afterFailure["webgpu_frames"]) ?? 0),
            "Replacement did not retain the renderer and its WebGPU frame counter")
        Thread.sleep(forTimeInterval: 0.45)
        _ = try prepare(clock, firstRevision + 1, "clock-after")
        let later = try commit(firstRevision + 1, "clock-after")
        let endTime = try check(later, clock, "clock-after", firstRevision + 2, rgb)[0][3]
        try require(endTime > startTime, "Renderer elapsed time did not advance")

        evidence.merge(["glStatus": glStatus, "first": first, "firstStatus": firstStatus,
                        "afterFailure": afterFailure, "replacement": replacement, "replacementStatus": replacementStatus,
                        "later": later, "clockSamples": [startTime, endTime]]) { _, new in new }
        if CommandLine.arguments.contains("--recovery") {
            let fail = "test-fail-first-draft-draw"
            _ = try prepare(quadrants, firstRevision + 2, fail)
            try rejects("draft_rolled_back") { try commit(firstRevision + 2, fail) }
            let rejected = try renderer.query("get_request", ["request_id": "commit-" + fail])
            try require((rejected["result"] as? WireObject)?["restored_previous"] as? Bool == true,
                        "Previous WebGPU draw was not restored")
            evidence["rejectedPresent"] = rejected
            evidence["afterRejectedPresent"] = try retained(firstRevision + 2, "clock-after",
                after: wireInt(replacementStatus["webgpu_frames"]) ?? 0)
        }
    }
    renderer.stop()

    // Also exercise the Swift host's usual prepare/commit wire path.
    let target = "verify-webgpu-m2a"
    let draft = Draft(target: target, queue: DispatchQueue(label: target))
    defer { draft.reset() }
    func request(_ method: String, _ params: WireObject = [:], _ state: WireObject = [:]) throws -> WireObject {
        var request: WireObject = ["id": UUID().uuidString, "version": 1, "method": method, "params": params, "target": target]
        request["epoch"] = state["epoch"]
        request["revision"] = state["revision"]
        return try draft.execute(Request(JSONSerialization.data(withJSONObject: request)))
    }
    let cold = try request("health")
    func hostPrepare(_ source: Data, _ state: WireObject, _ run: Int, language: String? = "wgsl") throws -> WireObject {
        var intent: WireObject = ["source": source.base64EncodedString(), "sha256": sha256Hex(source), "lease": lease,
                                  "documentId": documentID, "generation": 0, "editSequence": 0, "runSequence": run,
                                  "slots": [], "bindings": [:]]
        if let language = language { intent["language"] = language }
        return try request("run.prepare", intent, state)
    }
    if renderer.supportsDraftWgsl {
        let prepared = try hostPrepare(quadrants, cold, 1)
        let drawn = try request("run.commit", ["lease": lease, "token": prepared["token"] as? String ?? ""], prepared)
        try require(drawn["backend"] as? String == "webgpu" && drawn["sourceDigest"] as? String == sha256Hex(quadrants),
                    "Swift Host rejected or misreported the native WGSL receipt")
        evidence["swiftHost"] = drawn
    } else {
        try rejects("wgsl_unavailable") { try hostPrepare(quadrants, cold, 1) }
        let prepared = try hostPrepare(glsl, cold, 2, language: nil)
        let drawn = try request("run.commit", ["lease": lease, "token": prepared["token"] as? String ?? ""], prepared)
        try require((drawn["receipt"] as? WireObject)?["status"] as? String == "rendered",
                    "Swift Host could not run GLSL after WGSL rejection")
        evidence["swiftHost"] = drawn
    }
    let data = try JSONSerialization.data(withJSONObject: evidence, options: [.prettyPrinted, .sortedKeys])
    try data.write(to: URL(fileURLWithPath: CommandLine.arguments[2]), options: .atomic)
    print("PASS Swift DraftRenderer and Draft transaction (Dawn \(renderer.supportsDraftWgsl))")
}

do { try verify() }
catch let error as WireError { print("FAIL \(error.code) \(error.message)"); exit(1) }
catch { print("FAIL \(error)"); exit(1) }
