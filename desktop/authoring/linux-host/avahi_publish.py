#!/usr/bin/env python3
"""Keep one interface-scoped DNS-SD registration alive on the Avahi system bus."""

import ctypes as c
import select
import signal
import socket
import sys


def publish(interface, target, port):
    common = c.CDLL("libavahi-common.so.3")
    avahi = c.CDLL("libavahi-client.so.3")
    pointer, integer, string = c.c_void_p, c.c_int, c.c_char_p
    callback = c.CFUNCTYPE(None, pointer, integer, pointer)

    def api(lib, name, result, *args):
        fn = getattr(lib, "avahi_" + name)
        fn.restype, fn.argtypes = result, args
        return fn

    simple_poll_new = api(common, "simple_poll_new", pointer)
    simple_poll_get = api(common, "simple_poll_get", pointer, pointer)
    iterate = api(common, "simple_poll_iterate", integer, pointer, integer)
    stop_poll = api(common, "simple_poll_free", None, pointer)
    strerror = api(common, "strerror", string, integer)
    client_new = api(avahi, "client_new", pointer, pointer, integer, callback, pointer, c.POINTER(integer))
    client_free = api(avahi, "client_free", None, pointer)
    client_errno = api(avahi, "client_errno", integer, pointer)
    group_new = api(avahi, "entry_group_new", pointer, pointer, callback, pointer)
    group_commit = api(avahi, "entry_group_commit", integer, pointer)
    add_service = api(avahi, "entry_group_add_service_strlst", integer,
                      pointer, integer, integer, integer, string, string, string, string, c.c_uint16, pointer)
    add_txt = api(common, "string_list_add", pointer, pointer, string)
    free_txt = api(common, "string_list_free", None, pointer)

    avahi_ipv4 = 0
    client_running, group_established = 2, 2
    client_state, group_state = None, None
    running = True

    @callback
    def client_changed(_client, state, _user):
        nonlocal client_state
        client_state = state

    @callback
    def group_changed(_group, state, _user):
        nonlocal group_state
        group_state = state

    def stop(_signal, _frame):
        nonlocal running
        running = False

    def fail(code):
        raise RuntimeError(strerror(code).decode())

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    idx = socket.if_nametoindex(interface)
    poll = simple_poll_new()
    if not poll:
        raise RuntimeError("Could not allocate Avahi poll loop")
    client = None
    try:
        err = integer()
        client = client_new(simple_poll_get(poll), 0, client_changed, None, c.byref(err))
        if not client:
            fail(err.value)
        if client_state != client_running:
            raise RuntimeError("Avahi is not ready to publish a service")
        group = group_new(client, group_changed, None)
        if not group:
            fail(client_errno(client))
        txt = None
        try:
            for value in ("role=draft", "version=1", f"target={target}"):
                updated = add_txt(txt, value.encode())
                if not updated:
                    raise RuntimeError("Could not allocate the DNS-SD TXT record")
                txt = updated
            result = add_service(group, idx, avahi_ipv4, 0, f"VOSC {target[:8]}".encode(),
                                 b"_vosc-draft._tcp", b"local", None, port, txt)
        finally:
            free_txt(txt)
        if result < 0:
            fail(result)
        result = group_commit(group)
        if result < 0:
            fail(result)
        announced = False
        while running and not select.select([sys.stdin], [], [], 0)[0]:
            if iterate(poll, 100) != 0:
                raise RuntimeError("Avahi poll loop ended")
            if client_state != client_running or group_state in (3, 4):
                raise RuntimeError("Avahi registration lost or collided")
            if group_state == group_established and not announced:
                print("READY", flush=True)
                announced = True
    finally:
        if client:
            client_free(client)
        stop_poll(poll)


if __name__ == "__main__":
    try:
        publish(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    except (OSError, RuntimeError) as error:
        sys.exit(f"DNS-SD: {error}")
