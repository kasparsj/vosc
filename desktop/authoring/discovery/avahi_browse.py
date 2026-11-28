#!/usr/bin/env python3
"""Discover VOSC draft hosts through the Avahi system bus."""

import ctypes as c
import ipaddress
import json
import signal
import socket
import time
import uuid


SERVICE_TYPE = b"_vosc-draft._tcp"
MAX_SERVICES = 32
UNSPECIFIED = -1
CLIENT_FAILURE = 100
BROWSER_NEW, BROWSER_REMOVE, BROWSER_ALL_FOR_NOW, BROWSER_FAILURE = 0, 1, 3, 4
RESOLVER_FOUND = 0


def peer_from_record(name, address, interface, port, txt):
    fields = {}
    for entry in txt:
        key, equals, value = entry.partition(b"=")
        key = key.lower()
        if key in (b"role", b"version", b"target"):
            if not equals or key in fields:
                return None
            fields[key] = value
    if fields.get(b"role") != b"draft" or fields.get(b"version") != b"1":
        return None
    if not name or len(name) > 63 or not 1 <= port <= 65535:
        return None
    try:
        label = name.decode()
        target = fields.get(b"target", b"").decode("ascii")
        ip = ipaddress.ip_address(address)
        if str(uuid.UUID(target)) != target.lower() or not label.isprintable():
            return None
        if ip.is_unspecified or ip.is_multicast:
            return None
        host = str(ip)
        if ip.version == 6 and ip.is_link_local:
            host += "%" + socket.if_indextoname(interface)
    except (ValueError, OSError):
        return None
    return {"name": label, "target": target, "endpoint": {"host": host, "port": port}}


def browse():
    common = c.CDLL("libavahi-common.so.3")
    avahi = c.CDLL("libavahi-client.so.3")
    pointer, integer, string = c.c_void_p, c.c_int, c.c_char_p
    client_callback = c.CFUNCTYPE(None, pointer, integer, pointer)
    browser_callback = c.CFUNCTYPE(None, pointer, integer, integer, integer, string, string, string, integer, pointer)
    resolver_callback = c.CFUNCTYPE(None, pointer, integer, integer, integer, string, string, string, string,
                                   pointer, c.c_uint16, pointer, integer, pointer)

    def api(lib, name, result, *args):
        fn = getattr(lib, "avahi_" + name)
        fn.restype, fn.argtypes = result, args
        return fn

    poll_new = api(common, "simple_poll_new", pointer)
    poll_get = api(common, "simple_poll_get", pointer, pointer)
    poll_iterate = api(common, "simple_poll_iterate", integer, pointer, integer)
    poll_free = api(common, "simple_poll_free", None, pointer)
    strerror = api(common, "strerror", string, integer)
    client_new = api(avahi, "client_new", pointer, pointer, integer, client_callback, pointer, c.POINTER(integer))
    client_free = api(avahi, "client_free", None, pointer)
    client_errno = api(avahi, "client_errno", integer, pointer)
    browser_new = api(avahi, "service_browser_new", pointer, pointer, integer, integer, string, string, integer,
                      browser_callback, pointer)
    resolver_new = api(avahi, "service_resolver_new", pointer, pointer, integer, integer, string, string, string,
                       integer, integer, resolver_callback, pointer)
    resolver_free = api(avahi, "service_resolver_free", integer, pointer)
    address_text = api(common, "address_snprint", string, pointer, c.c_size_t, pointer)
    txt_next = api(common, "string_list_get_next", pointer, pointer)
    txt_size = api(common, "string_list_get_size", c.c_size_t, pointer)
    txt_data = api(common, "string_list_get_text", pointer, pointer)

    services = {}
    peers = {}
    all_for_now = False
    failure = None
    client = None

    def txt_items(txt):
        entries = []
        while txt and len(entries) < 64:
            size = txt_size(txt)
            if size > 255:
                return []
            entries.append(c.string_at(txt_data(txt), size))
            txt = txt_next(txt)
        return [] if txt else entries

    @client_callback
    def client_changed(connection, state, _user):
        nonlocal failure
        if state == CLIENT_FAILURE:
            failure = strerror(client_errno(connection)).decode()

    @resolver_callback
    def resolved(resolver, interface, _protocol, event, name, _service, _domain, _host, address, port, txt, _flags, _user):
        try:
            key = next((key for key, active in services.items() if active == resolver), None)
            if key is None:
                return
            services[key] = None
            if event != RESOLVER_FOUND or not address:
                return
            buffer = c.create_string_buffer(40)
            if address_text(buffer, len(buffer), address):
                peer = peer_from_record(name, buffer.value.decode("ascii"), interface, port, txt_items(txt))
                if peer:
                    peers[key] = peer
        finally:
            resolver_free(resolver)

    @browser_callback
    def browser_changed(_browser, interface, protocol, event, name, service, domain, _flags, _user):
        nonlocal all_for_now, failure
        key = (interface, protocol, name, service, domain)
        if event == BROWSER_NEW and key not in services and len(services) < MAX_SERVICES:
            services[key] = resolver_new(client, interface, protocol, name, service, domain, protocol, 0,
                                         resolved, None)
        elif event == BROWSER_REMOVE:
            peers.pop(key, None)
            resolver = services.pop(key, None)
            if resolver:
                resolver_free(resolver)
        elif event == BROWSER_ALL_FOR_NOW:
            all_for_now = True
        elif event == BROWSER_FAILURE:
            failure = strerror(client_errno(client)).decode()

    deadline = time.monotonic() + 4
    poll = poll_new()
    if not poll:
        raise RuntimeError("Could not start the Avahi poll loop")
    try:
        error = integer()
        client = client_new(poll_get(poll), 0, client_changed, None, c.byref(error))
        if not client:
            raise RuntimeError(strerror(error.value).decode())
        if not browser_new(client, UNSPECIFIED, UNSPECIFIED, SERVICE_TYPE, b"local", 0, browser_changed, None):
            raise RuntimeError(strerror(client_errno(client)).decode())
        while not failure and not (all_for_now and not any(services.values())):
            remaining = int((deadline - time.monotonic()) * 1000)
            if remaining <= 0:
                break
            if poll_iterate(poll, remaining) != 0:
                raise RuntimeError("The Avahi poll loop ended")
    finally:
        if client:
            client_free(client)
        poll_free(poll)

    ordered = sorted(peers.values(), key=lambda peer: (
        peer["target"].lower(), ":" in peer["endpoint"]["host"], peer["endpoint"]["host"],
    ))
    unique = {}
    for peer in ordered:
        unique.setdefault(peer["target"].lower(), peer)
    return {"peers": list(unique.values()), "error": error_message(failure) if failure else None}


def error_message(detail):
    return f"LAN discovery is unavailable (Avahi: {detail})."


if __name__ == "__main__":
    # Also bound synchronous calls to the system bus.
    signal.alarm(6)
    try:
        report = browse()
    except (OSError, RuntimeError) as error:
        report = {"peers": [], "error": error_message(error)}
    print(json.dumps(report))
