#!/usr/bin/env python3
"""Actual read-only export rejection of constructed unsupported feedback states."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import socket
import time

from test_feedback_composition import FIXTURES, checked, replay, setup_commands
from test_feedback_native_evidence import configure, hook, raw_replay
from test_scene_recipe import (ROOT, OwnedRenderer, arm_recovery_hook, capture, collect_cleanup,
                              free_udp_port, osc_bundle, source_snapshot, wait_recovery_hook)
from runtime_smoke import encode_message

PNG = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="


def ordinary(renderer, commands):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        sender.sendto(osc_bundle([encode_message(address, values) for address, values in commands]),
                      ("127.0.0.1", renderer.port))


def resource_snapshot(renderer):
    command = {"id": hashlib.sha256(str(time.monotonic_ns()).encode()).hexdigest(), "operation": "snapshot"}
    existing = arm_recovery_hook(renderer, command)
    return wait_recovery_hook(renderer, command, existing)["snapshot"]


def tiny_commands():
    return [(address, (0, 1., 1.) if address == "/tex/size" else values)
            for address, values in setup_commands(FIXTURES / "temporal.frag")]


def stable_scene(scene):
    return {key: value for key, value in scene.items() if key not in {"frame", "fps"}}


def stable_native(snapshot):
    return {"generation": snapshot["generation"], "allocation_pairs": snapshot["allocation_pairs"],
            "instances": snapshot["instances"]}


def wait_settled(renderer, previous_epoch):
    deadline = time.monotonic() + 5
    first_frame = None
    client = renderer.client()
    try:
        while time.monotonic() < deadline:
            scene = checked(client, "get_scene")
            if scene["external_epoch"] > previous_epoch:
                if first_frame is None:
                    first_frame = scene["frame"]
                if scene["frame"] >= first_frame + 3:
                    return scene
            time.sleep(.02)
    finally:
        client.close()
    raise TimeoutError("ordinary mutation did not settle over three rendered frames")


def reject_case(renderer, label, commands, expected_error, predicate, output, *, blue_output=False):
    configure(renderer)
    baseline_receipt = replay(renderer, tiny_commands())
    client = renderer.client()
    try:
        baseline = checked(client, "get_scene")
        positive = checked(client, "export_scene")
    finally:
        client.close()
    ordinary(renderer, commands)
    wait_settled(renderer, baseline["external_epoch"])
    before_native = hook(renderer, "feedback_snapshot")
    before_resources = resource_snapshot(renderer)
    client = renderer.client()
    try:
        before = checked(client, "get_scene")
        predicate(before, before_native, before_resources)
        response = client.call("export_scene", {})
        after = checked(client, "get_scene")
        output_capture = None
        if blue_output:
            width, height, pixels, output_capture = capture(client, output / (label + ".png"))
            bad = sum(max(abs(a-b) for a,b in zip(pixel,(0,0,255,255))) > 2 for pixel in pixels)
            if (width,height) != (800,600) or bad:
                raise AssertionError(f"pass was not actually rendered blue: {bad}")
    finally:
        client.close()
    after_native = hook(renderer, "feedback_snapshot")
    if response.get("ok") or response.get("error", {}).get("code") != "scene_unsupported":
        raise AssertionError(f"unsupported {label} did not reject export: {response}")
    if expected_error not in response["error"]["message"]:
        raise AssertionError(f"wrong rejection family for {label}: {response}")
    if stable_scene(before) != stable_scene(after) or stable_native(before_native) != stable_native(after_native):
        raise AssertionError(f"read-only export changed scene/source/storage/static updates: {label}")
    return {"baseline_receipt": baseline_receipt, "baseline_recipe": positive["recipe"],
            "commands": commands, "before": before, "native_before": before_native,
            "resources_before": before_resources, "response": response, "after": after,
            "native_after": after_native, "capture": output_capture}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def feedback(scene, layer=0, name="tex"):
    return scene["layers"][layer]["shader"]["textures"][name]["feedback"]


def rejected_holder(scene, native, resources):
    current = feedback(scene)
    require(not current["export_supported"] and current["updates"] >= 1,
            "mutated live feedback holder did not become unsupported")


def nested_public_path_case(renderer, label, commands):
    configure(renderer)
    replay(renderer,tiny_commands())
    client=renderer.client()
    try: baseline=checked(client,'get_scene')
    finally: client.close()
    ordinary(renderer,commands)
    wait_settled(renderer,baseline['external_epoch'])
    native_before=hook(renderer,'feedback_snapshot')
    client=renderer.client()
    try:
        before=checked(client,'get_scene')
        positive=checked(client,'export_scene')
        after=checked(client,'get_scene')
    finally: client.close()
    native_after=hook(renderer,'feedback_snapshot')
    require(feedback(before)['export_supported'],'ordinary unsupported nested route unexpectedly mutated feedback')
    require(feedback(before)['updates']==1,'nested attempt stepped static feedback')
    require(stable_scene(before)==stable_scene(after) and stable_native(native_before)==stable_native(native_after),
            'read-only export changed state after ignored nested route')
    configure(renderer)
    receipt=raw_replay(renderer,tiny_commands()+commands)
    native=hook(renderer,'feedback_snapshot')
    require(receipt['status']=='failed' and native['allocation_pairs']==0,
            'correlated unsupported nested prefix applied or allocated')
    return {'export_state_verdict':'PUBLIC_PATH_EXCLUDED',
        'reason':'Ordinary /tex/shader/* forwarding does not reach ShaderPingPongTex; no loaded nested state was fabricated.',
        'commands':commands,'baseline':baseline,'before':before,'successful_readonly_export':positive,
        'after':after,'native_before':native_before,'native_after':native_after,
        'correlated_rejection':receipt,'correlated_snapshot':native}


def looper_invalid_controls(renderer):
    results = {}
    for label, values, message in [
        ("zero-speed", (0,1.,1,0), "playback speed cannot be zero"),
        ("recording-overflow", (0,1.,2147483647,1), "unrepresentable recording rate"),
    ]:
        configure(renderer)
        replay(renderer,tiny_commands())
        client=renderer.client()
        try: before=checked(client,'get_scene')
        finally: client.close()
        offset=renderer.log.stat().st_size
        ordinary(renderer,[("/tex/looper",values)])
        after=wait_settled(renderer,before['external_epoch'])
        client=renderer.client()
        try: exported=checked(client,'export_scene')
        finally: client.close()
        require(feedback(after)['export_supported'],f"invalid looper control created unsupported state: {label}")
        require(feedback(after)['updates']==1,f"invalid looper control stepped feedback: {label}")
        with renderer.log.open('rb') as stream:
            stream.seek(offset);logs=stream.read().decode(errors='replace')
        require(message in logs,f"invalid looper control did not report expected rejection: {label}: {logs}")
        results[label]={'before':before,'after':after,'export':exported,'diagnostics':logs}
    configure(renderer)
    replay(renderer,tiny_commands())
    initial=resource_snapshot(renderer)
    ordinary(renderer,[("/tex/looper",(0,1.,1,1)),("/tex/set",(0,"needsUpdate",1))])
    wait_settled(renderer,initial['external_epoch'])
    client=renderer.client()
    try: before=checked(client,'get_scene')
    finally: client.close()
    require(not feedback(before)['export_supported'],'valid initial looper did not exist')
    offset=renderer.log.stat().st_size
    # Final rate fps/newSpeed fits; intermediate fps/currentSpeed overflows.
    ordinary(renderer,[("/tex/looper",(0,1.,2147483647,2))])
    after=wait_settled(renderer,before['external_epoch'])
    with renderer.log.open('rb') as stream:
        stream.seek(offset);logs=stream.read().decode(errors='replace')
    require('unrepresentable intermediate recording rate' in logs,'existing looper intermediate rate was not guarded')
    require(feedback(after)['updates']==feedback(before)['updates'],'rejected reconfiguration stepped feedback')
    results['existing-intermediate-overflow']={'before':before,'after':after,'diagnostics':logs}
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expect-sanitizer", action="append", choices=("address","undefined"), default=[])
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(mode=0o700, parents=True, exist_ok=False)
    plain = output / "plain.frag"
    plain.write_text("#version 330\nout vec4 fragColor;void main(){fragColor=vec4(.5,.25,.75,1);}\n")
    blue = output / "blue.frag"
    blue.write_text("#version 330\nout vec4 fragColor;void main(){fragColor=vec4(0,0,1,1);}\n")
    image = output / "pixel.png"; image.write_bytes(base64.b64decode(PNG))
    pass_consumer = output / "pass-consumer.frag"
    pass_consumer.write_text("#version 330\nuniform sampler2D tex;out vec4 fragColor;void main(){fragColor=texture(tex,vec2(.5));}\n")
    evidence = {"status":"FAILED", "source_before":source_snapshot(), "cases":{}}
    renderer = None
    try:
        renderer = OwnedRenderer(args.binary.resolve(), output, "export-rejections", free_udp_port(), args.expect_sanitizer)
        evidence["owner"] = renderer.owner
        (renderer.runtime / "shadertoy-api.json").write_bytes((FIXTURES / "shadertoy-api.json").read_bytes())
        common_shared = [("/tex/size",("sharedFeedback",1.,1.)),
            *[("/tex/set",("sharedFeedback",name,value)) for name,value in (
              ("internalformat",32856),("textureTarget",3553),("filter",9729),("static",1),("blendMode",0))],
            ("/tex",("sharedFeedback","shader_pp:"+str(plain))), ("/tex",(0,"sharedFeedback"))]
        def require_loaded_shadertoy(scene, _native, _resources):
            shader = scene["layers"][1]["shader"]
            require(shader["loaded"] and shader["program"] == 0
                    and shader["path"] == "shadertoy/VoscOfflineFixture.frag",
                    "offline fixture did not load as a Shadertoy shader")

        cases = {
            "shadertoy": ([
                ("/shader", (1, "https://www.shadertoy.com/view/VoscOfflineFixture")),
                ("/layer/visible", (1, 1)),
                ("/layer/alpha", (1, 1.0)),
            ], "Shadertoy shaders are outside editable recipe scope", require_loaded_shadertoy),
            "shared-feedback": (common_shared, "named object aliases", lambda s,n,r: (
                require(r["resources"]["texture_shared"] == 1,"shared pool not actually populated"),
                require(s["layers"][0]["shader"]["textures"]["tex"]["source"] == str(plain),"layer did not actually alias shared feedback"),
                require(feedback(s)["reason"] == "shared feedback is outside export scope","shared holder identity not observed"))),
            "named-feedback": ([("/shader/texture",(0,"extra","shader_pp:"+str(plain))),
                ("/shader/texture/size",(0,"extra",1.,1.)),
                *[("/shader/texture/set",(0,"extra",name,value)) for name,value in (("static",1),("internalformat",32856),("textureTarget",3553),("filter",9729),("blendMode",0))]],
                "feedback cannot be exported",lambda s,n,r: (
                    require(feedback(s,0,"extra")["updates"] == 1,"named feedback did not compile/execute"),
                    require(len(n["instances"]) == 2,"second actual feedback instance absent"))),
            "second-default-feedback": ([("/tex",(1,"shader_pp:"+str(plain))), ("/tex/size",(1,1.,1.)),
                *[("/tex/set",(1,name,value)) for name,value in (("static",1),("internalformat",32856),("textureTarget",3553),("filter",9729),("blendMode",0))]],
                "feedback cannot be exported",lambda s,n,r: (
                    require(feedback(s,1)["updates"] == 1,"second default feedback did not compile/execute"),
                    require(len(n["instances"]) == 2,"second actual feedback instance absent"))),
            "history": ([("/tex/set",(0,"numFrames",2)),("/tex/set",(0,"needsUpdate",1))], "feedback cannot be exported", rejected_holder),
            "looper": ([("/tex/looper",(0,1.,1,1)),("/tex/set",(0,"needsUpdate",1))], "feedback cannot be exported", rejected_holder),
            "looper-valid-reconfigure": ([("/tex/looper",(0,1.,1,1)),("/tex/looper",(0,1.,2,1)),("/tex/set",(0,"needsUpdate",1))], "feedback cannot be exported", rejected_holder),
            "passes": ([("/tex/passes",(0,str(blue))), ("/tex/set",(0,"needsUpdate",1)),
                        ("/shader",(0,str(pass_consumer)))], "named object aliases", lambda s,n,r: (
                            require(r["resources"]["shader_shared"] == 1,"pass shader was not retained"),
                            rejected_holder(s,n,r))),
            "nested-texture": ([("/tex/shader/texture",(0,"nested",str(image)))], "nested resources", lambda s,n,r: (
                require("nested resources" in feedback(s)["reason"],"nested texture not retained by shader"),rejected_holder(s,n,r))),
            "nested-buffer": ([("/tex/shader/buffer",(0,"nestedBuffer",.125,.25,.375,.5))], "nested resources", lambda s,n,r: (
                require("nested resources" in feedback(s)["reason"],"nested buffer not retained by shader"),rejected_holder(s,n,r))),
            "shader-owned-binding": ([("/tex/shader/var",(0,"owned",.5))], "shader-owned bindings", lambda s,n,r: (
                require("shader-owned bindings" in feedback(s)["reason"],"shader-owned control did not exist"),rejected_holder(s,n,r))),
            "camera": ([("/cam",("camera",))], "camera must be disabled", lambda s,n,r:require(s["camera_enabled"],"camera was not actually enabled")),
            "effects": ([("/shading/passes",("fxaa",))], "render passes", lambda s,n,r:require(not s["camera_enabled"],"camera masked effect rejection")),
            "custom-geometry": ([("/geom",(0,"box",1.,1.,1.))], "canonical quad", lambda s,n,r:require(s["layers"][0]["geometry_loaded"],"custom geometry failed to load")),
            "expression-control": ([("/tex/var",(0,"decay","t"))], "reflected float", lambda s,n,r: (
                require(feedback(s)["controls"]["decay"][0]["type"] == "s","expression binding was not retained"),rejected_holder(s,n,r))),
            "unsupported-storage": ([("/tex/set",(0,"internalformat",32849))], "storage", lambda s,n,r: (
                require(feedback(s)["internalformat"] == 32849,"unsupported configured format not applied"),rejected_holder(s,n,r))),
            "unsupported-target": ([("/tex/set",(0,"textureTarget",34037))], "storage", lambda s,n,r: (
                require(feedback(s)["textureTarget"] == 34037,"unsupported configured target not applied"),rejected_holder(s,n,r))),
            "unsupported-filter": ([("/tex/set",(0,"filter",9728))], "storage", lambda s,n,r: (
                require("storage" in feedback(s)["reason"],"unsupported filter did not affect configuration"),rejected_holder(s,n,r))),
            "unsupported-speed": ([("/tex/speed",(0,2.))], "holder settings", lambda s,n,r: (
                require("holder settings" in feedback(s)["reason"],"unsupported speed did not affect configuration"),rejected_holder(s,n,r))),
            "nonwhite-color": ([("/tex/color",(0,.5))], "holder settings", lambda s,n,r: (
                require("holder settings" in feedback(s)["reason"],"nonwhite color did not affect configuration"),rejected_holder(s,n,r))),
        }
        for case_index,(label,(commands,error,predicate)) in enumerate(cases.items()):
            try:
                if label in {'nested-texture','nested-buffer','shader-owned-binding'}:
                    result=nested_public_path_case(renderer,label,commands)
                    evidence["cases"][label]={"passed":True,"result":result}
                else:
                    result = reject_case(renderer,label,commands,error,predicate,output,blue_output=label in {"passes", "shadertoy"})
                    evidence["cases"][label]={"passed":True,"result":result}
            except Exception as error:
                evidence["cases"][label]={"passed":False,"error":repr(error)}
                if renderer.process.poll() is not None:
                    evidence["not_run"] = list(cases)[case_index + 1:]
                    break
        if renderer.process.poll() is None:
            try:
                evidence['looper_regressions']={'passed':True,'result':looper_invalid_controls(renderer)}
            except Exception as error:
                evidence['looper_regressions']={'passed':False,'error':repr(error)}
        else:
            evidence['looper_regressions']={'passed':False,'error':'owned renderer exited before looper regressions'}
        evidence["status"]="COLLECTED" if all(c["passed"] for c in evidence["cases"].values()) and evidence['looper_regressions']['passed'] else "FAILED"
    except Exception as error:
        evidence["error"]=repr(error)
    finally:
        if renderer:
            evidence["cleanup"],error=collect_cleanup(renderer)
            if error:evidence["cleanup_error"]=error;evidence["status"]="FAILED"
        evidence["source_after"]=source_snapshot()
        evidence["source_stable"]=evidence["source_before"]==evidence["source_after"]
        if not evidence["source_stable"]:evidence["status"]="FAILED"
        (output/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(evidence["status"],output)
    return 0 if evidence["status"]=="COLLECTED" else 1


if __name__=='__main__':raise SystemExit(main())
