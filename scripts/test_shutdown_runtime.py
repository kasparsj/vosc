#!/usr/bin/env python3
"""Exercise graceful shutdown of a private, hook-enabled renderer only."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def events(path):
    if not path.exists():
        return []
    # The producer appends one line; ignore only an incomplete final line.
    return [json.loads(line) for line in path.read_text().splitlines(keepends=True)
            if line.endswith('\n')]


def wait_for(check, process, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        if process.poll() is not None:
            raise RuntimeError(f'renderer exited early: {process.returncode}')
        time.sleep(.01)
    raise TimeoutError('renderer condition deadline expired')


def query(path):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(str(path))
        connection.sendall(b'{"id":"shutdown-check","method":"get_scene","params":{}}\n')
        with connection.makefile('rb') as stream:
            response = json.loads(stream.readline(8_388_609))
    if 'error' in response:
        raise RuntimeError(response)
    return response['result']


def send_onset(connection, port, value):
    connection.sendto(b'/onset\0\0' + b',i\0\0' + struct.pack('>i', value),
                      ('127.0.0.1', port))


def run_case(binary, output, name, active):
    runtime = output / name
    runtime.mkdir(mode=0o700)
    control = runtime / 'control.sock'
    if len(str(control).encode()) >= 104:
        raise ValueError('private Unix socket path too long')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    environment = {**os.environ, 'VOSC_CONTROL_SOCKET': str(control),
                   'VOSC_OSC_PORT': str(port), 'VOSC_RECOVERY_TEST_DIR': str(runtime),
                   'ASAN_OPTIONS': 'abort_on_error=1:halt_on_error=1:detect_leaks=0',
                   'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    receipt = {'case': name, 'binary': str(binary), 'binary_sha256': digest(binary),
               'osc_port': port, 'control_socket': str(control), 'status': 'FAILED'}
    stop = threading.Event()
    sender = None
    process = None
    try:
        with (runtime / 'renderer.log').open('wb') as stream:
            process = subprocess.Popen([str(binary)], cwd=binary.parents[3],
                                       env=environment, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            receipt['pid'] = process.pid
            wait_for(lambda: any(e['event'] == 'started' for e in events(runtime / 'events.jsonl')),
                     process)
            before = query(control)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                target = not before['onset_gate']
                send_onset(udp, port, int(target))
                applied = wait_for(lambda: (scene if (scene := query(control))['onset_gate'] == target
                                            else None), process)
                receipt['osc_applied'] = {'before': before, 'after': applied}

            sender_errors = []
            if active:
                def traffic():
                    try:
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                            while not stop.is_set():
                                send_onset(udp, port, 0)
                                stop.wait(.002)
                    except Exception as error:
                        sender_errors.append(repr(error))
                sender = threading.Thread(target=traffic)
                sender.start()
                # Confirm the traffic has reached the ordinary app router.
                wait_for(lambda: not query(control)['onset_gate'], process)
            else:
                # Observe another frame without sending any more OSC traffic.
                wait_for(lambda: query(control)['frame'] > applied['frame'], process)

            temporary = runtime / 'command.tmp'
            temporary.write_text(json.dumps({'id': name, 'operation': 'shutdown'}))
            temporary.replace(runtime / 'command.json')
            receipt['return_code'] = process.wait(timeout=15)
            stop.set()
            if sender:
                sender.join(timeout=2)
                if sender.is_alive() or sender_errors:
                    raise RuntimeError({'sender_errors': sender_errors, 'alive': sender.is_alive()})
            observed = events(runtime / 'events.jsonl')
            receipt['events'] = observed
            order = [event['event'] for event in observed]
            required = ['shutdown_requested', 'input_stopped', 'downstream_stopped']
            positions = [order.index(event) for event in required]
            if positions != sorted(positions) or any(order.count(event) != 1 for event in required):
                raise RuntimeError('shutdown phase order/count mismatch')
            if receipt['return_code'] != 0 or control.exists():
                raise RuntimeError('nonzero graceful exit or retained control socket')
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('0.0.0.0', port))
            log = (runtime / 'renderer.log').read_text(errors='replace')
            markers = ('ERROR: AddressSanitizer', 'SUMMARY: AddressSanitizer',
                       'runtime error:', 'UndefinedBehaviorSanitizer', 'ThreadSanitizer:')
            if any(marker in log for marker in markers):
                raise RuntimeError('sanitizer finding; see complete renderer.log')
            receipt.update(status='PASS', port_released=True, control_removed=True)
    except Exception as error:
        receipt['error'] = repr(error)
    finally:
        stop.set()
        if sender:
            sender.join(timeout=2)
        if process and process.poll() is None:
            # Popen owns this exact child; this path is always a failed test.
            receipt['forced_cleanup'] = True
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        (runtime / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-output', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build, output = args.build_output.resolve(), args.output.resolve()
    command = json.loads((build / 'command.json').read_text())
    if not any('VOSC_RECOVERY_TESTING=1' in part for part in command):
        raise ValueError('build must include recovery test hooks')
    if not any('-fsanitize=address,undefined' in part for part in command):
        raise ValueError('build must include address and undefined sanitizers')
    binary = build / 'products/voscDebug.app/Contents/MacOS/voscDebug'
    preservation = json.loads((build / 'preservation.json').read_text())
    receipt = json.loads((build / 'native-build-receipt.json').read_text())
    if preservation['return_code'] != 0 or not preservation['unchanged']:
        raise ValueError('build did not finish successfully with preserved inputs')
    if (receipt['status'] != 'built' or not receipt['recovery_test_hooks'] or
            receipt['sanitizers'] != ['address', 'undefined']):
        raise ValueError('successful instrumented hook-enabled build receipt required')
    if Path(receipt['binary']['path']).resolve() != binary or digest(binary) != receipt['binary']['sha256']:
        raise ValueError('binary does not match successful build receipt')
    if digest(Path(receipt['core']['path'])) != receipt['core']['sha256']:
        raise ValueError('core changed since build')
    for path, expected in receipt['source_sha256'].items():
        if digest(Path(path)) != expected:
            raise ValueError(f'source changed since build: {path}')
    for item in receipt['receiver_inputs']:
        if (digest(Path(item['source'])) != item['source_sha256'] or
                digest(Path(item['replacement'])) != item['replacement_sha256']):
            raise ValueError('receiver inputs changed since build')
    data = build / 'products/data'
    if not binary.is_file() or not data.is_dir() or data.is_symlink():
        raise ValueError('missing binary or private copied data')
    if any(path.is_symlink() for path in data.rglob('*')):
        raise ValueError('runtime data must be private copies, not shared symlinks')
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for name, active in [('idle', False), ('active', True), ('restart', False)]:
        result = run_case(binary, output, name, active)
        results.append(result)
        print(result['status'], name, flush=True)
        if result['status'] != 'PASS':
            break
    (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if len(results) == 3 and all(r['status'] == 'PASS' for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
