#!/usr/bin/env python3
"""Authenticate RPC sockets and relay admitted bytes to the private host bridge."""

import asyncio
from enum import IntEnum
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import ssl
import stat
import sys

SESSION_LIMIT = 16
HANDSHAKE_SECONDS = 15
RELAY_PROGRESS_SECONDS = 15
CONTROL_LIMIT = 256 * 1024
CHUNK_SIZE = 16 * 1024
ACK = b'{"ok":true}\n'


class Failure(IntEnum):
    IDENTITY = 10
    CONTROL = 11
    INITIAL_TRUST = 12
    PORT_IN_USE = 13
    ADDRESS_UNAVAILABLE = 14
    BIND = 15
    TRUST_UPDATE = 16


def read_identity(path):
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as source:
        metadata = os.fstat(source.fileno())
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != os.getuid() or stat.S_IMODE(metadata.st_mode) != 0o600:
            raise ValueError("Expected an owner-only identity file")
        payload = source.read(16_385)
    if len(payload) > 16_384:
        raise ValueError("Identity exceeds its size limit")
    identity = json.loads(payload)
    cert, key = identity["cert"], identity["key"]
    if not isinstance(cert, str) or not isinstance(key, str):
        raise ValueError("Invalid identity file")
    return cert, key


def trust_context(identity, peers):
    if not isinstance(peers, list) or len(peers) > 32:
        raise ValueError("Invalid peer list")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.maximum_version = ssl.TLSVersion.TLSv1_3
    context.verify_mode = ssl.CERT_REQUIRED
    context.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    context.num_tickets = 0
    key_fd = os.memfd_create("vosc-rpc-identity", os.MFD_CLOEXEC)
    try:
        os.fchmod(key_fd, 0o600)
        with os.fdopen(key_fd, "w", encoding="ascii", closefd=False) as pem:
            pem.write("\n".join(identity))
        context.load_cert_chain(f"/proc/self/fd/{key_fd}")
    finally:
        os.close(key_fd)
    pins = set()
    for leaf in peers:
        if not isinstance(leaf, str) or not (1 <= len(leaf) <= 6000):
            raise ValueError("Invalid approved certificate")
        pins.add(hashlib.sha256(ssl.PEM_cert_to_DER_cert(leaf.strip())).hexdigest())
    if peers:
        context.load_verify_locations(cadata="\n".join(peers))
    return context, pins


def write_ack(identifier, port=None):
    result = {"id": identifier}
    if port is not None:
        result["port"] = port
    print(json.dumps(result, separators=(",", ":")), flush=True)


async def configure(reader, first=False):
    line = await reader.readline()
    if not line:
        return None
    command = json.loads(line)
    fields = {"id", "peers", "token"} if first else {"id", "peers"}
    if not isinstance(command, dict) or command.keys() != fields:
        raise ValueError("Invalid control message")
    identifier = command["id"]
    if type(identifier) is not int and (not isinstance(identifier, str) or len(identifier) > 64):
        raise ValueError("Invalid control message ID")
    if first and (not isinstance(command["token"], str) or not re.fullmatch(r"[a-f0-9]{64}", command["token"])):
        raise ValueError("Invalid private bridge token")
    return command


async def bridge_check(reader, writer, message):
    writer.write(json.dumps(message, separators=(",", ":")).encode("ascii") + b"\n")
    await writer.drain()
    return await reader.readline() == ACK


async def secure_socket(raw, context, deadline):
    loop = asyncio.get_running_loop()
    reader = asyncio.StreamReader(limit=CHUNK_SIZE)
    protocol = asyncio.StreamReaderProtocol(reader)
    transport, _ = await loop.connect_accepted_socket(
        lambda: protocol, raw, ssl=context, ssl_handshake_timeout=max(0.001, deadline - loop.time())
    )
    return reader, asyncio.StreamWriter(transport, protocol, reader, loop)


async def relay(reader, writer):
    while chunk := await reader.read(CHUNK_SIZE):
        writer.write(chunk)
        async with asyncio.timeout(RELAY_PROGRESS_SECONDS):
            await writer.drain()


async def session(raw, deadline, unix_path, token, snapshot):
    bridge_writer = tls_writer = None
    tasks = []
    try:
        async with asyncio.timeout_at(deadline):
            bridge_reader, bridge_writer = await asyncio.open_unix_connection(unix_path, limit=CHUNK_SIZE)
            if not await bridge_check(bridge_reader, bridge_writer, {"token": token}):
                return

            # The host may shut down or rescind the reservation while TLS is handshaking.
            secure = asyncio.create_task(secure_socket(raw, snapshot()[0], deadline))
            bridge_finished = asyncio.create_task(bridge_reader.read(1))
            tasks = [secure, bridge_finished]
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            if secure in done:
                tls_reader, tls_writer = secure.result()
            if bridge_finished in done:
                return
            bridge_finished.cancel()
            await asyncio.gather(bridge_finished, return_exceptions=True)
            peer = tls_writer.get_extra_info("ssl_object")
            certificate = peer.getpeercert(binary_form=True)
            if peer.session_reused or not certificate:
                return
            fingerprint = hashlib.sha256(certificate).hexdigest()
            if fingerprint not in snapshot()[1]:
                return
            if not await bridge_check(bridge_reader, bridge_writer, {"fingerprint": fingerprint}):
                return

        tasks = [asyncio.create_task(relay(tls_reader, bridge_writer)),
                 asyncio.create_task(relay(bridge_reader, tls_writer))]
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    except (OSError, ValueError):
        # Rejected, interrupted and malformed connections are scoped to their own sockets.
        pass
    finally:
        for task in tasks:
            task.cancel()
        if bridge_writer:
            bridge_writer.transport.abort()
        if tls_writer:
            tls_writer.transport.abort()
        else:
            raw.close()
        if tasks:
            results = await asyncio.gather(*tasks, return_exceptions=True)
            # A handshake may complete concurrently with cancellation or a revoked reservation.
            if not tls_writer and isinstance(results[0], tuple):
                results[0][1].transport.abort()


async def accept(listener, unix_path, token, snapshot):
    loop = asyncio.get_running_loop()
    sessions = set()
    try:
        while True:
            raw, _ = await loop.sock_accept(listener)
            deadline = loop.time() + HANDSHAKE_SECONDS
            if len(sessions) >= SESSION_LIMIT:
                raw.close()
                continue
            active = asyncio.create_task(session(raw, deadline, unix_path, token, snapshot))
            sessions.add(active)
            active.add_done_callback(sessions.discard)
    finally:
        active = list(sessions)
        for task in active:
            task.cancel()
        await asyncio.gather(*active, return_exceptions=True)


async def serve(bind, port, unix_path, identity_path):
    try:
        identity = read_identity(identity_path)
    except (OSError, ValueError, KeyError, TypeError):
        return Failure.IDENTITY
    loop = asyncio.get_running_loop()
    main_task = asyncio.current_task()
    loop.add_signal_handler(signal.SIGTERM, main_task.cancel)
    loop.add_signal_handler(signal.SIGINT, main_task.cancel)
    control = asyncio.StreamReader(limit=CONTROL_LIMIT)
    control_pipe, _ = await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(control), sys.stdin.buffer)
    try:
        try:
            initial = await configure(control, first=True)
        except (OSError, ValueError):
            return Failure.CONTROL
        if initial is None:
            return
        try:
            current = trust_context(identity, initial["peers"])
        except (OSError, ValueError):
            return Failure.INITIAL_TRUST
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.setblocking(False)
            try:
                listener.bind((bind, port))
                listener.listen(SESSION_LIMIT)
            except OSError as error:
                return {
                    errno.EADDRINUSE: Failure.PORT_IN_USE,
                    errno.EADDRNOTAVAIL: Failure.ADDRESS_UNAVAILABLE,
                }.get(error.errno, Failure.BIND)
            async with asyncio.TaskGroup() as jobs:
                accepting = jobs.create_task(accept(listener, unix_path, initial["token"], lambda: current))
                try:
                    write_ack(initial["id"], listener.getsockname()[1])
                    while True:
                        try:
                            update = await configure(control)
                        except (OSError, ValueError):
                            return Failure.CONTROL
                        if update is None:
                            return
                        try:
                            current = trust_context(identity, update["peers"])
                        except (OSError, ValueError):
                            return Failure.TRUST_UPDATE
                        write_ack(update["id"])
                finally:
                    accepting.cancel()
    finally:
        control_pipe.close()


def main():
    os.umask(0o077)
    try:
        bind, port, unix_path, identity_path = sys.argv[1:]
        sys.exit(asyncio.run(serve(bind, int(port), unix_path, Path(identity_path))))
    except asyncio.CancelledError:
        return
    except Exception:
        # The parent maps fixed statuses to diagnostics without control or identity data.
        sys.exit(1)


if __name__ == "__main__":
    main()
