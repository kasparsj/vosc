#!/usr/bin/env python3
"""Exercise an installed render host with two disposable TLS peers."""

from __future__ import annotations

import argparse
import base64
from contextlib import ExitStack, closing, contextmanager
import hashlib
import json
import math
import os
import select
from pathlib import Path
import shlex
import signal
import socket
import ssl
import stat
import subprocess
import sys
import tempfile
from threading import Event, Thread
import time
import uuid

from verify_26_round_trip import await_ready, compare, write_json
from verify_draft_controls import FIXTURE, cadence, observe, own, require, scene, value
from test_macos_dmg import ReleaseRenderer, disposable_home
from test_scene_recipe import wait_socket
from control_client import Client


def read_reply(stream, request_id):
    body = stream.readline(256 * 1024 + 1)
    if not body or not body.endswith(b"\n"):
        raise ConnectionError("host closed before a bounded reply arrived")
    response = json.loads(body)
    require(response["id"] == request_id, "mismatched draft request receipt")
    return response


def identity(directory, ca):
    directory.mkdir(mode=0o700)
    key, certificate = (directory / name for name in ("key.pem", "certificate.pem"))
    subprocess.run(["/usr/bin/openssl", "ecparam", "-name", "prime256v1", "-param_enc", "named_curve",
                    "-genkey", "-noout", "-out", str(key)], capture_output=True, check=True)
    subprocess.run(["/usr/bin/openssl", "req", "-x509", "-new", "-key", str(key), "-out", str(certificate),
                    "-days", "1", "-subj", "/CN=VOSC Transport Verification"], capture_output=True, check=True)
    context = pinned_context(ca)
    context.load_cert_chain(certificate, key)
    der = ssl.PEM_cert_to_DER_cert(certificate.read_text())
    return context, der


def pinned_context(ca):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False  # The independently compared full leaf is the trust anchor.
    context.load_verify_locations(cadata=ssl.DER_cert_to_PEM_cert(ca))
    context.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    return context


def fingerprint(leaf):
    return hashlib.sha256(leaf).hexdigest()


class Wire:
    def __init__(self, args):
        self.address = args.address
        self.target = args.target
        self.bootstrap = args.port
        self.expected = args.fingerprint
        first = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        first.check_hostname = False
        first.verify_mode = ssl.CERT_NONE
        first.minimum_version = ssl.TLSVersion.TLSv1_3
        with socket.create_connection((self.address, self.bootstrap), timeout=5) as endpoint:
            with first.wrap_socket(endpoint, server_hostname=self.address) as connection:
                self.certificate = connection.getpeercert(binary_form=True)
        require(fingerprint(self.certificate) == self.expected, "independently displayed host fingerprint mismatch")
        self.pin = pinned_context(self.certificate)

    @contextmanager
    def connect(self, port, context):
        with socket.create_connection((self.address, port), timeout=6) as endpoint:
            with context.wrap_socket(endpoint, server_hostname=self.address) as connection:
                require(fingerprint(connection.getpeercert(binary_form=True)) == self.expected,
                        "authenticated endpoint changed the host certificate")
                yield connection

    def send(self, connection, method, params=None, *, state=None, **changes):
        message = {"id": uuid.uuid4().hex, "method": method, "params": params or {},
                   "version": 1, "target": self.target}
        if state is not None:
            message.update(epoch=state["epoch"], revision=state["revision"])
        message.update(changes)
        connection.sendall(json.dumps(message).encode() + b"\n")
        return message["id"]

    def call(self, port, context, method, params=None, **fields):
        with self.connect(port, context) as connection, connection.makefile("rb") as stream:
            return read_reply(stream, self.send(connection, method, params, **fields))


def accepted(reply):
    require(reply["ok"], reply)
    return reply["result"]


def rejection(wire, port, context):
    try:
        reply = wire.call(port, context, "health")
    except (ConnectionError, ssl.SSLError) as error:
        return type(error).__name__
    raise RuntimeError(f"unapproved TLS configuration reached authenticated health: {reply}")


def owned_draft(host_pid, binary):
    rows = subprocess.check_output(["ps", "-axo", "pid=,ppid=,command="], text=True)
    matches = []
    for row in rows.splitlines():
        pid, parent, command = row.strip().split(maxsplit=2)
        if int(parent) == host_pid and command.startswith(str(binary) + " --draft-data-root "):
            arguments = shlex.split(command.removeprefix(str(binary) + " "))
            if "--draft-preview" in arguments:
                matches.append((int(pid), Path(arguments[1]), command))
    require(len(matches) == 1, "expected exactly one owned native preview under the specified host")
    return matches[0]


def linux_process(pid):
    proc = Path("/proc") / str(pid)
    require(pid > 1 and proc.stat().st_uid == os.getuid(), "expected a process owned by the harness user")
    fields = (proc / "stat").read_text().rsplit(")", 1)[1].split()
    arguments = [os.fsdecode(part) for part in (proc / "cmdline").read_bytes().rstrip(b"\0").split(b"\0")]
    return {"pid": pid, "parent": int(fields[1]), "start_ticks": int(fields[19]),
            "executable": os.readlink(proc / "exe"), "arguments": arguments}


def linux_preview(host, binary):
    require(linux_process(host["pid"]) == host, "Linux host identity changed")
    task = Path("/proc") / str(host["pid"]) / "task"
    children = set()
    for thread in task.iterdir():
        try:
            children.update(map(int, (thread / "children").read_text().split()))
        except FileNotFoundError:
            continue
    matches = []
    for pid in children:
        try:
            child = linux_process(pid)
        except FileNotFoundError:
            continue
        if (child["parent"] == host["pid"] and child["executable"] == str(binary)
                and child["arguments"][:2] == [str(binary), "--draft-data-root"]
                and "--draft-preview" in child["arguments"][3:]):
            matches.append(child)
    require(len(matches) == 1, "expected one owned Linux native preview")
    child = matches[0]
    data = Path(child["arguments"][2])
    root = data.parent
    require(root.parent == Path("/tmp") and root.name.startswith("vosc-draft-") and data.name == "data",
            "unexpected Linux native data location")
    for directory in (root, data):
        info = directory.lstat()
        require(stat.S_ISDIR(info.st_mode) and stat.S_IMODE(info.st_mode) == 0o700
                and info.st_uid == os.getuid(), "expected a private native directory")
    return child, data


@contextmanager
def closing_pidfd(pid):
    descriptor = os.pidfd_open(pid)
    try:
        yield descriptor
    finally:
        os.close(descriptor)


def linux_control(child, control):
    info = control.lstat()
    require(stat.S_ISSOCK(info.st_mode) and stat.S_IMODE(info.st_mode) == 0o600
            and info.st_uid == os.getuid(), "expected a private native socket")
    proc = Path("/proc") / str(child["pid"])
    owned = set()
    for descriptor in (proc / "fd").iterdir():
        try:
            owned.add(os.readlink(descriptor))
        except FileNotFoundError:
            continue
    rows = (proc / "net/unix").read_text().splitlines()[1:]
    require(any(len(fields := row.split(maxsplit=7)) == 8 and fields[7] == str(control)
                and f"socket:[{fields[6]}]" in owned for row in rows),
            "control socket is not owned by the identified native renderer")


@contextmanager
def independent_show(args, binary, manifest, scratch, output, report):
    report["show"] = {}
    if args.linux_install:
        with ExitStack() as stack:
            _, client = own(stack, report, binary, output, "show", ["--scene", manifest])
            yield client
    else:
        home, _ = disposable_home(scratch, "show-home")
        separate = ReleaseRenderer(binary, manifest, scratch, output / "show.log", home)
        with separate.run(report["show"]):
            await_ready(separate, "26.frag — wire-independent-show")
            wait_socket(separate.process, separate.socket, separate.log)
            with closing(separate.client()) as client:
                yield client


def prepare(wire, rpc, context, state, source, *, lease, document, sequence, slots=(), bindings=None):
    request_id = uuid.uuid4().hex
    data = {
        **source, "lease": lease, "documentId": document, "generation": 1,
        "editSequence": sequence - 1, "runSequence": sequence,
        "slots": list(slots), "bindings": bindings or {},
    }
    result = accepted(wire.call(rpc, context, "run.prepare", data, state=state, id=request_id))
    require(result["token"] == request_id and result["lease"] == lease
            and result["sourceDigest"] == source["sha256"] and result["runSequence"] == sequence, result)
    return result


def commit(wire, rpc, context, state, staged, *, lose_reply=False):
    request_id = uuid.uuid4().hex
    args = {"lease": staged["lease"], "token": staged["token"]}
    if lose_reply:
        with wire.connect(rpc, context) as connection:
            wire.send(connection, "run.commit", args, state=state, id=request_id)
            # Confirm the request hit the host before dropping the unread reply.
            retained(wire, rpc, context, request_id, {"pending", "completed"})
        reply = retained(wire, rpc, context, request_id, {"completed"})["response"]
        duplicate = wire.call(rpc, context, "run.commit", args, state=state, id=request_id)
        require(duplicate == reply, "identical retry changed a retained commit")
        conflicting = wire.call(rpc, context, "run.commit", {**args, "token": uuid.uuid4().hex},
                                state=state, id=request_id)
        require(conflicting["error"]["code"] == "id_conflict", conflicting)
    else:
        reply = wire.call(rpc, context, "run.commit", args, state=state, id=request_id)
    result = accepted(reply)
    receipt = result["receipt"]
    draw = receipt["result"]["scene"]
    require(result["commitID"] == receipt["id"] == request_id
            and result["lease"] == staged["lease"] and result["token"] == staged["token"]
            and result["sourceDigest"] == staged["sourceDigest"]
            and result["controlDigest"] == staged["controlDigest"]
            and receipt["status"] == "rendered"
            and draw["frame"] == receipt["frame"] and draw["rendered_revision"] == result["revision"], result)
    return result


def retained(wire, rpc, context, request_id, wanted):
    until = time.monotonic() + 4
    while time.monotonic() < until:
        result = accepted(wire.call(rpc, context, "receipt.get", {"id": request_id}))
        if result["state"] in wanted:
            return result
        time.sleep(0.05)
    raise RuntimeError(f"request {request_id} did not reach receipt state {wanted}")


@contextmanager
def drop_native_replies(control, request_id):
    """Drop exactly the typed mutation reply and its first lookup on an owned socket."""
    direct = control.with_name("direct.sock")
    require(control.is_socket() and not direct.exists(), "expected an owned private native listening socket")
    control.rename(direct)
    received = []
    failures = []
    stopped = Event()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    proxy = None

    def relay():
        while not stopped.is_set():
            try:
                caller, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError as error:
                if not stopped.is_set():
                    failures.append(str(error))
                return
            try:
                with caller, caller.makefile("rb") as incoming, socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as child:
                    caller.settimeout(2)
                    line = incoming.readline(256 * 1024 + 1)
                    if not line:
                        continue
                    require(line.endswith(b"\n"), "private request was not bounded")
                    request = json.loads(line)
                    child.settimeout(2)
                    child.connect(str(direct))
                    child.sendall(line)
                    with child.makefile("rb") as outbound:
                        reply = outbound.readline(8 * 1024 * 1024 + 1)
                    require(reply.endswith(b"\n"), "owned native endpoint returned no bounded reply")
                    selected = (
                        not received and request["id"] == request_id and request["method"] == "draft_configure_slot"
                    ) or (
                        len(received) == 1 and request["method"] == "get_request"
                        and request["params"]["request_id"] == request_id
                    )
                    if selected:
                        received.append({"method": request["method"], "request": request["id"],
                                         "native": json.loads(reply)})
                    else:
                        caller.sendall(reply)
            except (OSError, ValueError, RuntimeError) as error:
                failures.append(str(error))

    try:
        listener.bind(str(control))
        os.chmod(control, 0o600)
        listener.listen()
        listener.settimeout(0.1)
        proxy = Thread(target=relay, name="owned-native-test-proxy")
        proxy.start()
        yield received
    finally:
        stopped.set()
        listener.close()
        if proxy is not None:
            proxy.join(timeout=5)
            require(not proxy.is_alive(), "owned fault proxy did not stop")
        control.unlink(missing_ok=True)
        direct.rename(control)
    require(not failures, failures)


def wait_for_renderer_reset(wire, rpc, context, epoch, root, *, linux_exit=None):
    until = time.monotonic() + 5
    allow_transport_until = time.monotonic() + 0.75
    while time.monotonic() < until:
        reply = wire.call(rpc, context, "health")
        if (linux_exit is not None and not reply["ok"]
                and reply["error"]["code"] == "renderer_transport"
                and time.monotonic() < allow_transport_until):
            linux_exit["transient_health_errors"].append(reply["error"]["code"])
            time.sleep(0.05)
            continue
        state = accepted(reply)
        if state["epoch"] != epoch and state["needsRestore"] and not root.exists():
            return state
        time.sleep(0.05)
    raise RuntimeError("owned renderer did not invalidate its epoch and clean up on process loss")

def run(args, output, report):
    linux_host = None
    if args.linux_install:
        install = args.linux_install.resolve(strict=True)
        host_entry = install / "host.cjs"
        binary = (install / "renderer/vosc").resolve(strict=True)
        linux_host = linux_process(args.host_pid)
        require(Path(linux_host["executable"]).name == "node"
                and linux_host["arguments"][1:3] == [str(host_entry), "serve"] and host_entry.is_file(),
                "host PID does not run the specified installed Linux entry point")
        require(hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal"),
                "Linux PID file descriptor support is required for the renderer signal checks")
        report["linux_host"] = linux_host
    else:
        host_binary = args.app.resolve(strict=True) / "Contents/MacOS/VOSC Render Host"
        started = subprocess.check_output(["ps", "-p", str(args.host_pid), "-o", "comm="], text=True).strip()
        require(started == str(host_binary), "host PID does not own the specified installed companion")
        binary = args.app.resolve(strict=True).parent / "vosc.app/Contents/MacOS/vosc"
    wire = Wire(args)
    hello = accepted(wire.call(wire.bootstrap, wire.pin, "hello"))
    require((hello["role"], hello["version"], hello["target"]) == ("draft", 1, args.target), hello)
    rpc = hello["rpcPort"]
    report["hello"] = hello
    report["host_certificate_sha256"] = args.fingerprint
    report["unauthenticated_rejected"] = rejection(wire, rpc, wire.pin)
    with tempfile.TemporaryDirectory(prefix="vosc-wire-proof-", dir="/tmp") as directory:
        scratch = Path(directory).resolve()
        a, der_a = identity(scratch / "key-a", wire.certificate)
        b, der_b = identity(scratch / "key-b", wire.certificate)
        report["unapproved_rejected"] = rejection(wire, rpc, a)
        pairings = []
        for certificate in (der_a, der_b):
            result = accepted(wire.call(wire.bootstrap, wire.pin, "pair.start", {
                "name": "Transport verification", "certificate": base64.b64encode(certificate).decode(),
            }))
            require(result["fingerprint"] == fingerprint(certificate), result)
            pairings.append(result)
        report["pairings"] = pairings
        first, second = pairings
        require(first["fingerprint"] != second["fingerprint"] and first["request"] != second["request"], pairings)
        print("Approve only the first request after comparing its public certificate.", flush=True)
        print(json.dumps(pairings, indent=2), flush=True)

        def pairing_status(entry):
            response = wire.call(wire.bootstrap, wire.pin, "pair.status", {"request": entry["request"]})
            return accepted(response)["state"]

        decision = pairing_status(first)
        while decision == "pending":
            time.sleep(0.4)
            require(time.time() * 1000 < first["expiresAt"], "first pairing was not approved in time")
            decision = pairing_status(first)
        require(decision == "approved", "first controller certificate was not approved")
        require(pairing_status(first) == "expired", "approval result was not consumed once")
        report["unapproved_duplicate_rejected"] = rejection(wire, rpc, b)
        report["wrong_target"] = wire.call(rpc, a, "health", target=str(uuid.uuid4()))
        report["wrong_version"] = wire.call(rpc, a, "health", version=2)
        require(report["wrong_target"]["error"]["code"] == "wrong_target"
                and report["wrong_version"]["error"]["code"] == "incompatible_protocol",
                "target or version mismatch was accepted")

        source = args.shader.read_bytes()
        data = {"source": base64.b64encode(source).decode(), "sha256": fingerprint(source)}
        show_scene = scene(output, "wire-independent-show", FIXTURE)
        with independent_show(args, binary, show_scene, scratch, output, report) as show:
            show_first = observe(show, "show-first", output, report)
            report["show_baseline_cadence"] = cadence(show)
            state = accepted(wire.call(rpc, a, "health"))
            document = str(uuid.uuid4())
            lease = str(uuid.uuid4())
            staged = prepare(wire, rpc, a, state, data, lease=lease, document=document, sequence=1)
            state = commit(wire, rpc, a, state, staged, lose_reply=True)
            report["initial_prepare"] = staged
            report["lost_commit_reply"] = state
            report["show_concurrent_cadence"] = cadence(show)
            child = None
            if linux_host:
                child, root = linux_preview(linux_host, binary)
                pid, command = child["pid"], shlex.join(child["arguments"])
            else:
                pid, root, command = owned_draft(args.host_pid, binary)
            report["native_preview"] = {"pid": pid, "root": str(root), "command": command}
            control = root.parent / "control.sock"
            if child:
                report["native_preview"]["linux_identity"] = child
                linux_control(child, control)

            def assert_owned():
                if child:
                    require(linux_process(linux_host["pid"]) == linux_host and linux_process(pid) == child,
                            "Linux preview or host identity changed during the test")
                else:
                    require(owned_draft(args.host_pid, binary)[0] == pid, "draft owner changed during the test")

            def signal_preview(sig):
                assert_owned()
                if child:
                    with closing_pidfd(pid) as descriptor:
                        assert_owned()
                        signal.pidfd_send_signal(descriptor, sig)
                else:
                    subprocess.run(["/bin/kill", "-" + sig.name.removeprefix("SIG"), str(pid)], check=True)

            native = Client(control)
            with closing(native):
                begin = observe(native, "before-typed-edit", output, report)
                tone = next(item for item in state["uniforms"] if item["name"] == "tone")["values"]
                lost_params = {"name": "tone", "component": 1, "mode": "fixed", "value": 0.73}
                with wire.connect(rpc, a) as losing:
                    lost_id = wire.send(losing, "uniform.set", lost_params, state=state)
                    for _ in range(12):
                        found = accepted(wire.call(rpc, a, "receipt.get", {"id": lost_id}))
                        if found["state"] in {"pending", "completed"}: break
                        time.sleep(0.05)
                    require(found["state"] in {"pending", "completed"}, "lost request never reached the host")
                for _ in range(24):
                    found = accepted(wire.call(rpc, a, "receipt.get", {"id": lost_id}))
                    if found["state"] == "completed": break
                    time.sleep(0.05)
                state = accepted(found["response"])
                observed = next(item for item in state["uniforms"] if item["name"] == "tone")["values"]
                require(tone[0] == observed[0] and tone[2] == observed[2]
                        and math.isclose(observed[1], 0.73, abs_tol=1e-7), observed)
                duplicate = wire.call(rpc, a, "uniform.set", {**lost_params, "value": 0.12},
                                      state=state, id=lost_id)
                require(duplicate["error"]["code"] == "id_conflict", duplicate)
                report.update(lost_reply=found, id_conflict=duplicate)
                current = observe(native, "after-typed-edit", output, report)
                require(compare(begin, current)["pixels_changed_by_at_least_16"] > 5_000, "fixed edit was invisible")
                state = accepted(wire.call(rpc, a, "slot.configure", {"id": "sTransport", "label": "Transport", "default": 0},
                                           state=state))
                state = accepted(wire.call(rpc, a, "uniform.set", {"name": "gain", "component": 0, "mode": "range",
                                                                  "slot": "sTransport", "low": 0.2, "high": 0.8},
                                           state=state))
                report["samples"] = []
                frames = []
                for index, (sample, expected) in enumerate(((0, 0.2), (0.5, 0.5), (1, 0.8))):
                    sampled = accepted(wire.call(rpc, a, "sample", {
                        "lease": lease, "sequence": index + 1, "slot": "sTransport", "value": sample,
                    }, state=state))
                    gain = next(uniform["values"][0] for uniform in sampled["uniforms"] if uniform["name"] == "gain")
                    require(sampled["revision"] == state["revision"] and sampled["samples"]["sTransport"] == sample,
                            sampled)
                    require(math.isclose(gain, expected, abs_tol=1e-6), f"Stale sample draw readback: {sampled}")
                    label = f"transport-sample-{index}"
                    frames.append(observe(native, label, output, report))
                    require(math.isclose(value(report[label]["state"], "gain"), expected, abs_tol=1e-6), sampled)
                    report["samples"].append(sampled)
                require(all(compare(low, high)["pixels_changed_by_at_least_16"] > 5_000
                            for low, high in zip(frames[:-1], frames[1:], strict=True)), "mapped range was invisible")
            show_checkpoint = observe(show, "show-before-stall", output, report)
            before = time.monotonic()
            signal_preview(signal.SIGSTOP)
            try:
                timeout = wire.call(rpc, a, "uniform.set", {
                    "name": "gain", "component": 0, "mode": "fixed", "value": 0.6,
                }, state=state)
            finally:
                if root.exists() and (child or owned_draft(args.host_pid, binary)[0] == pid):
                    signal_preview(signal.SIGCONT)
            report["timeout"] = {"elapsed": time.monotonic() - before, "response": timeout}
            require(not timeout["ok"] and timeout["error"]["code"] == "renderer_timeout", timeout)
            live = accepted(wire.call(rpc, a, "health"))
            assert_owned()
            require(root.exists() and live["epoch"] == state["epoch"] and live["commitID"] == state["commitID"]
                    and live["revision"] == state["revision"], "lost response reset the healthy private renderer")
            report["healthy_after_timeout"] = live
            # A real native request is forwarded, but the host loses the reply
            # and the first lookup. Recovery must use the original native ID.
            slot_request = uuid.uuid4().hex
            assert_owned()
            if child:
                linux_control(child, control)
            with drop_native_replies(control, slot_request) as native_wire:
                before = time.monotonic()
                delayed = wire.call(rpc, a, "slot.configure",
                                    {"id": "sDelayed", "label": "Delayed", "default": 0.25},
                                    state=state, id=slot_request)
                delayed_elapsed = time.monotonic() - before
                require(not delayed["ok"] and delayed["error"]["code"] == "renderer_uncertain", delayed)
                require([step["method"] for step in native_wire] == ["draft_configure_slot", "get_request"]
                        and native_wire[0]["native"]["ok"]
                        and native_wire[1]["native"]["result"]["id"] == slot_request, native_wire)
                recovered = retained(wire, rpc, a, slot_request, {"completed"})["response"]
            changed = accepted(recovered)
            slot_draw = changed["receipt"]
            drawn_scene = slot_draw["result"]["scene"]
            assert_owned()
            require(changed["epoch"] == state["epoch"] and changed["revision"] == state["revision"] + 1
                    and slot_draw["id"] == slot_request and slot_draw["status"] == "rendered"
                    and drawn_scene["frame"] == slot_draw["frame"]
                    and drawn_scene["rendered_revision"] == changed["revision"]
                    and drawn_scene["draft_slots"]["sDelayed"] == 0.25, recovered)
            report["late_control"] = {"elapsed": delayed_elapsed, "unconfirmed": delayed, "confirmed": recovered,
                                      "dropped_native_replies": native_wire}
            state = changed
            show_after = observe(show, "show-after-stall", output, report)
            require(compare(show_first, show_checkpoint)["max_channel_error"] == 0
                    and compare(show_checkpoint, show_after)["max_channel_error"] == 0
                    and all(report["show-first"]["state"][key] == report["show-after-stall"]["state"][key]
                            for key in ("layers", "revision", "external_epoch"))
                    and report["show-after-stall"]["state"]["frame"] > report["show-before-stall"]["state"]["frame"],
                    "separate show did not keep drawing unchanged through the draft stall")
            linux_exit = None
            if child:
                with closing_pidfd(pid) as descriptor:
                    assert_owned()
                    exited = select.poll()
                    exited.register(descriptor, select.POLLIN)
                    signal.pidfd_send_signal(descriptor, signal.SIGTERM)
                    require(any(events & select.POLLIN for _, events in exited.poll(5000)),
                            "owned Linux preview did not exit after SIGTERM")
                linux_exit = report["native_preview_exit"] = {
                    "pid": pid, "start_ticks": child["start_ticks"], "confirmed_by": "pidfd",
                    "transient_health_errors": [],
                }
            else:
                signal_preview(signal.SIGTERM)
            fresh = wait_for_renderer_reset(wire, rpc, a, state["epoch"], root, linux_exit=linux_exit)
            stale = wire.call(rpc, a, "sample", {
                "lease": lease, "sequence": 4, "slot": "sTransport", "value": 0.2,
            }, state=state)
            require(stale["error"]["code"] in {"stale_epoch", "stale_lease"}, stale)
            unknown = accepted(wire.call(rpc, a, "receipt.get", {"id": lost_id}))
            require(unknown["state"] == "unknown", "stale receipt survived renderer loss")
            staged = prepare(wire, rpc, a, fresh, data, lease=str(uuid.uuid4()), document=document, sequence=2,
                             slots=[{"id": "sTransport", "label": "Transport", "default": 0}],
                             bindings={"gain": {"type": "float", "components": [
                                 {"slot": "sTransport", "low": 0.2, "high": 0.8},
                             ]}})
            rehydrated = commit(wire, rpc, a, fresh, staged)
            require(rehydrated["epoch"] == fresh["epoch"] and rehydrated["samples"]["sTransport"] == 0,
                    rehydrated)
            report.update(stale_epoch=stale, stale_receipt=unknown, reactivated=rehydrated)

        while time.time() * 1000 <= second["expiresAt"]:
            time.sleep(0.4)
        report["expired_pairing"] = pairing_status(second)
        require(report["expired_pairing"] == "expired" and rejection(wire, rpc, b), "expired key was admitted")

        with wire.connect(rpc, a) as waiting, waiting.makefile("rb") as stream:
            accepted(read_reply(stream, wire.send(waiting, "health")))
            print("Revoke only this paired fixture. The connection remains open for verification.", flush=True)
            print(first["fingerprint"], flush=True)
            for _ in range(60):
                try:
                    reply = read_reply(stream, wire.send(waiting, "health"))
                    require(reply["ok"], reply)
                except (ConnectionError, ssl.SSLError) as error:
                    report["live_session_revoked"] = type(error).__name__
                    break
                time.sleep(0.4)
            else: raise RuntimeError("live session did not close after revocation")
        report["revoked_reconnect_rejected"] = rejection(wire, rpc, a)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--fingerprint", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--host-pid", required=True, type=int)
    install = parser.add_mutually_exclusive_group(required=True)
    install.add_argument("--app", type=Path, help="signed macOS Render Host app")
    install.add_argument("--linux-install", type=Path, help="staged Linux Render Host installation")
    for name in ("shader", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    if args.linux_install and sys.platform != "linux":
        parser.error("--linux-install requires Linux process ownership checks")
    args.output.mkdir(parents=True)
    report = {"command": [sys.executable, *sys.argv], "status": "open"}
    try:
        run(args, args.output.resolve(), report)
        report["status"] = "pass"
    except Exception as error:
        report.update(status="fail", error=str(error))
        raise
    finally:
        write_json(args.output / "report.json", report)
