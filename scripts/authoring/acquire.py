"""Bounded acquisition of source bytes and provider provenance for shader import."""

from __future__ import annotations

from dataclasses import dataclass, field
from http.client import HTTPException
import io
import json
from pathlib import Path
import re
import stat
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote, unquote, urlencode, urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener
import zipfile


MAX_SOURCE_BYTES = 128 * 1024
_FRAGMENT_SUFFIXES = {".frag", ".fs", ".fsh", ".glsl"}
_GITHUB_ARCHIVE = frozenset({"api.github.com", "codeload.github.com"})
_GITHUB_API = frozenset({"api.github.com"})
_GITHUB_RAW = frozenset({"raw.githubusercontent.com"})
_SHADERTOY = frozenset({"www.shadertoy.com"})
_SEGMENT = re.compile(r"[A-Za-z0-9_.-]+")
_COMMIT = re.compile(r"[a-f0-9]{40}")


class ImportFailure(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class Limits:
    source_bytes: int = MAX_SOURCE_BYTES
    response_bytes: int = 16 * 1024 * 1024
    total_bytes: int = 32 * 1024 * 1024
    archive_entries: int = 2500
    archive_bytes: int = 32 * 1024 * 1024
    seconds: float = 45.0


@dataclass(frozen=True)
class Candidate:
    origin: dict
    source: bytes | None
    error: ImportFailure | None = None


@dataclass
class Batch:
    origin: dict
    candidates: list[Candidate] = field(default_factory=list)
    complete: bool = True
    error: ImportFailure | None = None

    def incomplete(self, error):
        self.complete = False
        self.error = error


def https_url(url, hosts):
    if any(ord(ch) <= 32 or ord(ch) >= 127 or ch == "\\" for ch in url):
        raise ImportFailure("invalid_url", "URL must use encoded HTTPS path components")
    try:
        value = urlsplit(url)
        if value.scheme != "https" or value.netloc not in hosts or value.fragment:
            raise ValueError()
    except ValueError:
        raise ImportFailure("invalid_url", "Expected an HTTPS URL on the selected provider") from None
    return value


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class Fetch:
    """One bounded acquisition, including redirects and all provider responses."""

    def __init__(self, limits):
        self.limits = limits
        self.deadline = time.monotonic() + limits.seconds
        self.bytes = 0
        self.opener = build_opener(ProxyHandler({}), NoRedirect())

    def remaining(self):
        seconds = self.deadline - time.monotonic()
        if seconds <= 0:
            raise ImportFailure("acquisition_timeout", "Acquisition deadline expired")
        return seconds

    def __call__(self, url, hosts, limit, *, redirects=0):
        for attempt in range(redirects + 1):
            https_url(url, hosts)
            available = min(limit, self.limits.response_bytes, self.limits.total_bytes - self.bytes)
            if available <= 0:
                raise ImportFailure("acquisition_limit", "Aggregate download limit reached")
            request = Request(url, headers={"User-Agent": "VOSC-authoring",
                                            "Accept-Encoding": "identity"})
            try:
                with self.opener.open(request, timeout=min(self.remaining(), 8)) as response:
                    length = response.headers.get("Content-Length", "")
                    if length.isdecimal() and int(length) > available:
                        raise ImportFailure("acquisition_limit", "Provider response exceeds the byte limit")
                    chunks = []
                    received = 0
                    while True:
                        self.remaining()
                        block = response.read1(min(65536, available - received + 1))
                        self.remaining()
                        if not block:
                            if length.isdecimal() and int(length) != received:
                                raise ImportFailure("acquisition_failed", "Provider response was truncated")
                            return b"".join(chunks)
                        received += len(block)
                        self.bytes += len(block)
                        if received > available:
                            raise ImportFailure("acquisition_limit", "Provider response exceeds the byte limit")
                        chunks.append(block)
            except HTTPError as error:
                with error:
                    if error.code in (301, 302, 303, 307, 308) and attempt < redirects:
                        target = error.headers.get("Location", "")
                        try:
                            https_url(target, hosts)
                        except ImportFailure:
                            raise ImportFailure("acquisition_failed", "Provider redirect was rejected") from None
                        url = target
                        continue
                    raise ImportFailure("acquisition_failed", f"Provider returned HTTP {error.code}") from None
            except (TimeoutError, URLError, OSError, HTTPException) as error:
                reason = getattr(error, "reason", error)
                if isinstance(reason, TimeoutError):
                    raise ImportFailure("acquisition_timeout", "Provider request timed out") from None
                raise ImportFailure("acquisition_failed", "Unable to read provider response") from None
        raise ImportFailure("acquisition_failed", "Provider redirect limit reached")


def json_object(raw):
    try:
        value = json.loads(raw)
    except (ValueError, RecursionError):
        raise ImportFailure("acquisition_failed", "Provider response is not valid JSON") from None
    if not isinstance(value, dict):
        raise ImportFailure("acquisition_failed", "Provider response is not a JSON object")
    return value


def components(path):
    if not path.startswith("/") or path.startswith("//"):
        raise ImportFailure("invalid_url", "Missing provider path")
    values = [unquote(part, errors="strict") for part in path.removesuffix("/")[1:].split("/")]
    if any(not part or part in {".", ".."} or "\\" in part or
           any(ord(ch) < 32 or ord(ch) == 127 for ch in part) for part in values):
        raise ImportFailure("invalid_url", "Unsafe provider path")
    return values


def repo_parts(parts):
    owner, repo = parts[0], parts[1].removesuffix(".git")
    if not all(_SEGMENT.fullmatch(part) and part not in {".", ".."} for part in (owner, repo)):
        raise ImportFailure("invalid_url", "Invalid GitHub repository path")
    return owner.lower(), repo.lower()


def resolve_ref(fetch, owner, repo, ref):
    if (not ref or len(ref) > 250 or "\\" in ref or
            any(part in {"", ".", ".."} for part in ref.split("/")) or
            any(ord(ch) < 32 or ord(ch) == 127 for ch in ref)):
        raise ImportFailure("invalid_url", "Invalid Git ref")
    api = f"https://api.github.com/repos/{owner}/{repo}/commits/{quote(ref, safe='')}"
    value = json_object(fetch(api, _GITHUB_API, 2 * 1024 * 1024))
    commit = value.get("sha")
    if not isinstance(commit, str) or not _COMMIT.fullmatch(commit):
        raise ImportFailure("acquisition_failed", "GitHub did not resolve the requested ref")
    if _COMMIT.fullmatch(ref) and commit != ref:
        raise ImportFailure("acquisition_failed", "GitHub resolved a different pinned commit")
    return {"provider": "github", "repository": f"https://github.com/{owner}/{repo}",
            "ref": ref, "commit": commit}


def github_file(url, fetch, limits):
    parsed = https_url(url, frozenset({"github.com", "raw.githubusercontent.com"}))
    parts = components(parsed.path)
    if parsed.netloc == "github.com" and len(parts) >= 5 and parts[2] == "blob":
        owner, repo = repo_parts(parts)
        ref, *path = parts[3:]
    elif parsed.netloc == "raw.githubusercontent.com" and len(parts) >= 4:
        owner, repo = repo_parts(parts)
        ref, *path = parts[2:]
    else:
        raise ImportFailure("invalid_url", "Expected a GitHub blob or raw source URL")
    if parsed.query or any("/" in part for part in path):
        raise ImportFailure("invalid_url", "Use a GitHub file path and percent-encode any slash in its ref")
    origin = resolve_ref(fetch, owner, repo, ref)
    escaped = "/".join(quote(piece, safe="") for piece in path)
    raw = f"https://raw.githubusercontent.com/{owner}/{repo}/{origin['commit']}/{escaped}"
    source = fetch(raw, _GITHUB_RAW, limits.source_bytes)
    return Batch(origin, [Candidate({**origin, "path": "/".join(path), "url": raw}, source)])


def archive_candidates(raw, batch, limits, remaining):
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            if len(entries) > limits.archive_entries:
                raise ImportFailure("acquisition_limit", "Archive entry count exceeds the scan limit")
            if sum(entry.file_size for entry in entries) > limits.archive_bytes:
                raise ImportFailure("acquisition_limit", "Expanded archive exceeds the byte limit")
            roots = {entry.orig_filename.split("/", 1)[0] for entry in entries}
            if len(roots) != 1:
                raise ImportFailure("acquisition_failed", "GitHub archive has an unexpected root layout")
            seen = set()
            for entry in sorted(entries, key=lambda item: item.orig_filename):
                remaining()
                original = entry.orig_filename
                _, separator, name = original.partition("/")
                parts = name.split("/")
                if ("\x00" in original or "\\" in original or
                        any(part in {".", ".."} or not part for part in parts[:-1]) or
                        (not separator and not entry.is_dir())):
                    raise ImportFailure("acquisition_failed", "Archive contains an unsafe member path")
                if entry.is_dir() or Path(name).suffix.lower() not in _FRAGMENT_SUFFIXES:
                    continue
                if name in seen:
                    raise ImportFailure("acquisition_failed", "Archive contains duplicate shader paths")
                seen.add(name)
                origin = {**batch.origin, "path": name}
                file_type = stat.S_IFMT(entry.external_attr >> 16)
                if file_type not in (0, stat.S_IFREG):
                    candidate = Candidate(origin, None, ImportFailure("unsupported_dependency",
                                                                      "Archive member is not a regular file"))
                elif entry.file_size > limits.source_bytes:
                    candidate = Candidate(origin, None, ImportFailure("source_limit",
                                                                      "Shader exceeds the source byte limit"))
                else:
                    with archive.open(entry) as shader:
                        source = shader.read(limits.source_bytes + 1)
                    if len(source) != entry.file_size:
                        raise ImportFailure("acquisition_failed", "Shader archive member size is inconsistent")
                    candidate = Candidate(origin, source)
                batch.candidates.append(candidate)
    except ImportFailure as error:
        batch.incomplete(error)
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile, NotImplementedError):
        batch.incomplete(ImportFailure("acquisition_failed", "Unable to read the GitHub ZIP archive"))


def github_repository(url, ref, fetch, limits):
    parsed = https_url(url, frozenset({"github.com"}))
    parts = components(parsed.path)
    if len(parts) != 2 or parsed.query:
        raise ImportFailure("invalid_url", "Expected a GitHub repository URL and an explicit ref")
    owner, repo = repo_parts(parts)
    origin = resolve_ref(fetch, owner, repo, ref)
    api = f"https://api.github.com/repos/{owner}/{repo}/zipball/{origin['commit']}"
    batch = Batch(origin)
    try:
        archive = fetch(api, _GITHUB_ARCHIVE, limits.response_bytes, redirects=2)
    except ImportFailure as error:
        batch.incomplete(error)
        return batch
    archive_candidates(archive, batch, limits, fetch.remaining)
    return batch


class ShadertoyAPI:
    """Documented API boundary. A test provider may supply a response object."""

    def __init__(self, key):
        self.key = key

    def __call__(self, shader_id, fetch):
        if not self.key:
            raise ImportFailure("provider_credentials", "Shadertoy API access requires a configured key")
        api = f"https://www.shadertoy.com/api/v1/shaders/{shader_id}?{urlencode({'key': self.key})}"
        return json_object(fetch(api, _SHADERTOY, 2 * 1024 * 1024))


def shadertoy_url(url, provider, fetch, limits):
    parsed = https_url(url, frozenset({"www.shadertoy.com", "shadertoy.com"}))
    match = re.fullmatch(r"/view/([A-Za-z0-9]{6})/?", parsed.path)
    if not match or parsed.query:
        raise ImportFailure("invalid_url", "Expected a Shadertoy /view/ URL")
    shader_id = match[1]
    response = provider(shader_id, fetch)
    if not isinstance(response, dict):
        raise ImportFailure("acquisition_failed", "Shadertoy response is not an object")
    if "Error" in response or "error" in response:
        message = response.get("Error", response.get("error"))
        lower = message.lower() if isinstance(message, str) else ""
        if "key" in lower or "unauthoriz" in lower:
            raise ImportFailure("provider_credentials", "Shadertoy rejected API authorization")
        if "not found" in lower:
            raise ImportFailure("acquisition_failed", "Shadertoy reports that the shader is unavailable")
        raise ImportFailure("acquisition_failed", "Shadertoy reported an API body error")
    shader = response.get("Shader")
    if not isinstance(shader, dict) or not isinstance(shader.get("info"), dict):
        raise ImportFailure("acquisition_failed", "Shadertoy response omitted shader identity")
    if shader["info"].get("id") != shader_id:
        raise ImportFailure("acquisition_failed", "Shadertoy returned a different shader identity")
    passes = shader.get("renderpass")
    if (not isinstance(passes, list) or len(passes) != 1 or not isinstance(passes[0], dict) or
            passes[0].get("type") != "image" or passes[0].get("name") != "Image"):
        raise ImportFailure("unsupported_pass", "Shadertoy import requires exactly one Image pass")
    image = passes[0]
    if image.get("inputs") != []:
        raise ImportFailure("unsupported_channel", "Shadertoy Image must declare an empty input channel list")
    source = image.get("code")
    if not isinstance(source, str):
        raise ImportFailure("acquisition_failed", "Shadertoy Image source is missing")
    try:
        data = source.encode("utf-8")
    except UnicodeError:
        raise ImportFailure("malformed_source", "Shadertoy Image source is not valid Unicode") from None
    if len(data) > limits.source_bytes:
        raise ImportFailure("source_limit", "Shadertoy Image exceeds the source byte limit")
    origin = {"provider": "shadertoy", "shader_id": shader_id,
              "url": f"https://www.shadertoy.com/view/{shader_id}", "path": "Image.frag"}
    return Batch(origin, [Candidate(origin, data)])


def acquire(kind, value, *, ref=None, limits=Limits(), shadertoy=None, fetch=None):
    fetch = fetch or Fetch(limits)
    batch = Batch({"provider": kind})
    try:
        if kind == "repository":
            return github_repository(value, ref, fetch, limits)
        if kind == "github":
            return github_file(value, fetch, limits)
        if kind == "shadertoy":
            return shadertoy_url(value, shadertoy or ShadertoyAPI(None), fetch, limits)
        if kind == "file":
            path = Path(value).resolve(strict=True)
            if not path.is_file():
                raise ImportFailure("acquisition_failed", "Selected source path is not a regular file")
            with path.open("rb") as stream:
                source = stream.read(limits.source_bytes + 1)
            batch.origin.update({"path": path.name, "url": path.as_uri()})
        elif kind == "paste":
            source = value
            batch.origin["path"] = "pasted.frag"
        else:
            raise ValueError(f"Unknown import kind: {kind}")
        if not isinstance(source, bytes):
            raise ImportFailure("malformed_source", "Shader source must be UTF-8 bytes")
        if len(source) > limits.source_bytes:
            raise ImportFailure("source_limit", "Shader exceeds the source byte limit")
        batch.candidates.append(Candidate(batch.origin, source))
    except ImportFailure as error:
        batch.incomplete(error)
    except (OSError, UnicodeError):
        batch.incomplete(ImportFailure("acquisition_failed", "Unable to read the requested shader source"))
    return batch
