import Foundation

func require(_ value: Bool, _ message: String) throws {
    if !value { throw WireError("verification_failed", message) }
}

func verify() throws {
    guard CommandLine.arguments.count == 3 else {
        throw WireError("verification_failed", "Usage: verify-webgpu-m1 <WGSL fixture> <evidence.json>")
    }
    let source = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
    let renderer = try DraftRenderer(validation: false)
    defer { renderer.stop() }
    func status() throws -> WireObject { try renderer.query("draft_m1_status") }
    func readPixels() throws -> (Int, Data) {
        let receipt = try renderer.mutate("capture_preview", ["max_width": 64], expecting: "captured")
        return (wireInt(receipt["frame"]) ?? -1,
                Data(base64Encoded: (receipt["result"] as? WireObject)?["data_base64"] as? String ?? "") ?? Data())
    }
    func nextFrame(_ key: String, _ previous: Int) throws -> WireObject {
        for _ in 0..<45 {
            let state = try status()
            if (wireInt(state[key]) ?? 0) > previous { return state }
            Thread.sleep(forTimeInterval: 0.04)
        }
        throw WireError("verification_failed", "No advancing frame in \(key)")
    }
    func check(_ state: WireObject, _ webgpu: Bool) throws {
        try require(wireInt(state["pid"]) == Int(renderer.process.processIdentifier), "Renderer PID changed")
        try require(state["active"] as? String == (webgpu ? "webgpu" : "opengl"), "Unexpected active renderer")
        try require(state["backend"] as? String == (webgpu ? "Metal" : "OpenGL"), "Unexpected GPU backend")
        try require(!(state["adapter"] as? String ?? "").isEmpty, "Missing GPU adapter")
        try require(state["opengl_visible"] as? Bool == !webgpu && state["webgpu_visible"] as? Bool == webgpu,
                    "Unexpected window visibility")
    }
    let glsl = "#version 150\nout vec4 fragColor;\nvoid main() { fragColor = vec4(0.125, 0.25, 0.75, 1.0); }\n"
    _ = try renderer.mutate("publish_shader", ["layer": 0, "source": glsl])
    let first = try status()
    try check(first, false)
    let (beforeFrame, beforePng) = try readPixels()
    try require(beforePng.starts(with: [137, 80, 78, 71, 13, 10, 26, 10]), "OpenGL capture missing PNG")
    do {
        _ = try renderer.mutate("draft_m1_switch",
            ["backend": "webgpu", "source": "@fragment fn fs_main() -> @location(0) vec4f { unavailable(); }"])
        throw WireError("verification_failed", "Broken candidate was accepted")
    } catch let error as WireError where error.code == "m1_switch_failed" {
        print("Rejected candidate \(error.message.prefix(120))")
    }
    let afterFailure = try nextFrame("opengl_frame", wireInt(first["opengl_frame"]) ?? 0)
    try check(afterFailure, false)
    let (failureFrame, failurePng) = try readPixels()
    try require(failureFrame > beforeFrame && beforePng == failurePng, "OpenGL changed after rejection")
    let activation = try renderer.mutate("draft_m1_switch", ["backend": "webgpu", "source": source])
    let metal = activation["result"] as? WireObject ?? [:]
    try check(metal, true)
    let samples = (metal["pixels"] as? [WireObject] ?? []).compactMap { $0["rgba"] as? [Int] }
    try require(samples == [[255, 64, 0, 255], [0, 128, 255, 255], [191, 0, 128, 255], [0, 255, 64, 255]],
                "Unexpected Dawn pixel samples \(samples)")
    let continued = try nextFrame("webgpu_frames", wireInt(metal["webgpu_frames"]) ?? 0)
    try check(continued, true)
    let restoredReceipt = try renderer.mutate("draft_m1_switch", ["backend": "opengl"])
    let restored = restoredReceipt["result"] as? WireObject ?? [:]
    try check(restored, false)
    let (finalFrame, finalPng) = try readPixels()
    try require(finalFrame > failureFrame && finalPng == beforePng, "Retained OpenGL capture changed")
    let evidence: WireObject = ["initial": first, "afterFailure": afterFailure, "metal": metal, "continued": continued,
        "restored": restored, "glCaptureFrames": [beforeFrame, failureFrame, finalFrame]]
    let data = try JSONSerialization.data(withJSONObject: evidence, options: [.prettyPrinted, .sortedKeys])
    try data.write(to: URL(fileURLWithPath: CommandLine.arguments[2]), options: .atomic)
    print("PASS Host DraftRenderer PID \(renderer.process.processIdentifier) OpenGL → Metal → OpenGL")
}

do { try verify() }
catch let error as WireError { print("FAIL \(error.code) \(error.message)"); exit(1) }
catch { print("FAIL \(error)"); exit(1) }
