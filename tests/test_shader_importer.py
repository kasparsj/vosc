"""Focused provider, source identity and transaction checks for shader import."""

from copy import deepcopy
from errno import ENOSPC
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from scripts.authoring import import_shader as importer
from scripts.authoring.acquire import ImportFailure, Limits, acquire
from scripts.authoring.source_profiles import SHADERTOY_VERSION


NATIVE = b"""#version 330
out vec4 color;
void main() { color = vec4(gl_FragCoord.x / 160.0, 0.0, 0.0, 1.0); }
"""
LEGACY = b"""#version 120
// gl_FragColor stays a comment.
void main() { gl_FragColor = vec4(0.0, 0.5, 0.0, 1.0); }
"""
SHADERTOY = {
    "Shader": {
        "info": {"id": "Abc123"},
        "renderpass": [{
            "name": "Image", "type": "image", "inputs": [],
            "code": "void mainImage(out vec4 color, in vec2 pixel) {\n"
                    "    color = vec4(pixel / iResolution.xy, sin(iTime) * 0.5 + 0.5, 1.0);\n}\n",
        }],
    },
}
REPO = "https://github.com/example/shaders"
READY = {"ok": True, "code": "native_linked", "message": "Native program linked and drew"}
COMMIT_ONE, COMMIT_TWO = "a" * 40, "b" * 40


class FixtureFetch:
    def __init__(self, commit=COMMIT_ONE, files=(), failure=None):
        self.commit = commit
        self.failure = failure
        self.urls = []
        output = io.BytesIO()
        with ZipFile(output, "w") as archive:
            for name, source in files:
                archive.writestr(f"example-shaders-{commit}/{name}", source)
        self.archive = output.getvalue()

    def remaining(self):
        return 1.0

    def __call__(self, url, *args, **kwargs):
        self.urls.append(url)
        if "/commits/" in url:
            return json.dumps({"sha": self.commit}).encode()
        if self.failure:
            raise self.failure
        return self.archive


class ShaderImporterTests(unittest.TestCase):
    def test_repository_ref_is_resolved_once_and_each_candidate_has_a_result(self):
        fetch = FixtureFetch(files=[("z.frag", LEGACY),
                                    ("a.frag", NATIVE.replace(b"out vec4", b"in vec2 custom; out vec4")),
                                    ("c.frag", NATIVE), ("README.md", b"ignored")])
        batch = acquire("repository", REPO, ref="main", fetch=fetch)
        prepared = importer.prepare(batch)
        self.assertTrue(batch.complete)
        self.assertEqual(len(fetch.urls), 2)
        self.assertTrue(fetch.urls[0].endswith("/commits/main"))
        self.assertTrue(fetch.urls[1].endswith(f"/zipball/{COMMIT_ONE}"))
        self.assertEqual([item.origin["path"] for item in prepared], ["a.frag", "c.frag", "z.frag"])
        self.assertEqual(prepared[0].error.code, "unsupported_dependency")
        self.assertEqual(prepared[1].conversion.source, NATIVE)
        self.assertIn(b"// gl_FragColor stays a comment.", prepared[2].conversion.source)
        self.assertIn(b"out vec4 voscFragmentColor;", prepared[2].conversion.source)

    def test_malformed_input_limits_and_download_failure(self):
        malformed = acquire("github", "http://github.com/example/shaders/blob/main/z.frag")
        self.assertEqual(malformed.error.code, "invalid_url")
        malformed = importer.prepare(acquire("paste", b""))[0]
        self.assertEqual(malformed.error.code, "malformed_source")
        unsupported = importer.prepare(acquire("paste", NATIVE.replace(b"330", b"450")))[0]
        self.assertEqual(unsupported.error.code, "unsupported_profile")
        no_shader_limit = acquire("repository", REPO, ref="main", limits=Limits(archive_entries=1),
                                  fetch=FixtureFetch(files=[("a.frag", NATIVE), ("b.frag", LEGACY)]))
        self.assertFalse(no_shader_limit.complete)
        self.assertEqual(no_shader_limit.error.code, "acquisition_limit")
        download_failure = acquire("repository", REPO, ref="main",
                                   fetch=FixtureFetch(failure=ImportFailure("acquisition_failed", "HTTP 503")))
        self.assertFalse(download_failure.complete)
        self.assertEqual(download_failure.error.code, "acquisition_failed")
        timeout = acquire("repository", REPO, ref="main",
                          fetch=FixtureFetch(failure=ImportFailure("acquisition_timeout", "Timed out")))
        self.assertFalse(timeout.complete)
        self.assertEqual(timeout.error.code, "acquisition_timeout")

    def test_texture_dependencies_are_rejected_in_every_source_profile(self):
        native_types = ("sampler2D", "sampler3D", "sampler2DArray", "isampler2D",
                        "usampler2DMSArray", "samplerCubeArrayShadow", "samplerBuffer",
                        "image2D", "iimageCube", "uimage2DRect")
        for texture_type in native_types:
            with self.subTest(profile="native", texture_type=texture_type):
                declaration = f"uniform {texture_type} tex;\n".encode()
                shader = NATIVE.replace(b"out vec4 color;\n", declaration + b"out vec4 color;\n")
                item, = importer.prepare(acquire("paste", shader))
                self.assertEqual(item.error.code, "unsupported_dependency")
                self.assertTrue(str(item.error).endswith(texture_type))

        legacy = LEGACY.replace(b"void main()", b"#define TEX sampler2D\nuniform TEX tex;\nvoid main()")
        item, = importer.prepare(acquire("paste", legacy))
        self.assertEqual(item.error.code, "unsupported_dependency")
        self.assertTrue(str(item.error).endswith("sampler2D"))

        payload = deepcopy(SHADERTOY)
        renderpass = payload["Shader"]["renderpass"][0]
        renderpass["code"] = "uniform sampler2D tex;\n" + renderpass["code"]
        batch = acquire("shadertoy", "https://www.shadertoy.com/view/Abc123",
                        shadertoy=lambda shader_id, fetch: payload)
        item, = importer.prepare(batch)
        self.assertEqual(item.error.code, "unsupported_dependency")
        self.assertTrue(str(item.error).endswith("sampler2D"))

    def test_texture_like_identifiers_and_comments_are_not_dependencies(self):
        shader = NATIVE.replace(b"out vec4 color;", b"""// sampler2D and image2D
uniform float sampler2DScale, samplerCubePhase, imageTint;
out vec4 color;""")
        item, = importer.prepare(acquire("paste", shader))
        self.assertEqual(item.conversion.source, shader)

    def test_retry_preserves_artist_copy_and_distinct_revisions(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory)
            files = [("a.frag", NATIVE), ("bad.frag", b"invalid")]
            first = importer.prepare(acquire("repository", REPO, ref="main", fetch=FixtureFetch(files=files)))
            self.assertEqual(importer.publish(library, first, [READY, None]), ["published", None])
            first_id = first[0].entry()["id"]
            draft = library / "shaders" / first_id / "shader.frag"
            draft.write_bytes(b"artist draft")
            self.assertEqual(importer.publish(library, first, [READY, None]), ["reused_edited", None])
            self.assertEqual(list(importer.read_catalog(library)), [first_id])

            revised = importer.prepare(acquire("repository", REPO, ref="main",
                                              fetch=FixtureFetch(COMMIT_TWO, files)))
            self.assertEqual(importer.publish(library, revised, [READY, None]), ["published", None])
            changed = importer.prepare(acquire("paste", NATIVE.replace(b"160.0", b"200.0")))
            self.assertEqual(importer.publish(library, changed, [READY]), ["published"])
            self.assertEqual(len(importer.read_catalog(library)), 3)
            self.assertEqual(draft.read_bytes(), b"artist draft")
            self.assertNotEqual(first_id, revised[0].entry()["id"])

    def test_atomic_publish_recovers_when_catalog_write_fails_after_promotion(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory)
            initial = importer.prepare(acquire("paste", NATIVE))
            importer.publish(library, initial, [READY])
            manifest = library / importer.CATALOG
            prior = manifest.read_bytes()
            candidate = importer.prepare(acquire("paste", LEGACY))
            target = library / "shaders" / candidate[0].entry()["id"]
            write = importer.write_file

            def fail_catalog(path, data):
                if path.name == importer.CATALOG:
                    raise OSError(ENOSPC, "No space left on device")
                write(path, data)

            with patch.object(importer, "write_file", side_effect=fail_catalog):
                with self.assertRaisesRegex(ImportFailure, "No space left on device"):
                    importer.publish(library, candidate, [READY])
            self.assertEqual(manifest.read_bytes(), prior)
            self.assertEqual({path.name for path in target.iterdir()},
                             {"original.frag", "generated.frag", "shader.frag", "entry.json"})
            draft = target / "shader.frag"
            draft.write_bytes(b"new edit after interruption")
            self.assertEqual(importer.publish(library, candidate, [READY]), ["reused_edited"])
            self.assertEqual(len(importer.read_catalog(library)), 2)
            self.assertEqual(draft.read_bytes(), b"new edit after interruption")

    def test_exclusive_publish_preserves_a_racing_empty_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory)
            candidate = importer.prepare(acquire("paste", NATIVE))
            promote = importer.promote_directory

            def race(source, target):
                target.mkdir()
                return promote(source, target)

            with patch.object(importer, "promote_directory", side_effect=race):
                self.assertEqual(importer.publish(library, candidate, [READY]), ["collision"])
            target = library / "shaders" / candidate[0].entry()["id"]
            self.assertEqual(list(target.iterdir()), [])
            self.assertFalse((library / importer.CATALOG).exists())

    def test_shadertoy_fixture_admission_and_provider_diagnostics(self):
        url = "https://www.shadertoy.com/view/Abc123"

        def response(value):
            return acquire("shadertoy", url, shadertoy=lambda shader_id, fetch: value)

        good, = importer.prepare(response(SHADERTOY))
        self.assertEqual(good.conversion.version, SHADERTOY_VERSION)
        self.assertIn(b"vec3(resolution, 1.0)", good.conversion.source)
        self.assertIn(b"sin((time))", good.conversion.source)
        self.assertIn(b"mainImage(voscImageColor, gl_FragCoord.xy)", good.conversion.source)
        for payload, code in [({"Error": "Invalid key"}, "provider_credentials"),
                              ({"Error": "Shader not found"}, "acquisition_failed"),
                              ({}, "acquisition_failed")]:
            with self.subTest(payload=payload):
                self.assertEqual(response(payload).error.code, code)
        extra_pass = deepcopy(SHADERTOY)
        extra_pass["Shader"]["renderpass"].append({"type": "buffer"})
        self.assertEqual(response(extra_pass).error.code, "unsupported_pass")
        channel = deepcopy(SHADERTOY)
        channel["Shader"]["renderpass"][0]["inputs"] = [{"channel": 0, "ctype": "texture"}]
        self.assertEqual(response(channel).error.code, "unsupported_channel")
        mouse = deepcopy(SHADERTOY)
        mouse["Shader"]["renderpass"][0]["code"] += "\nfloat x() { return iMouse.x; }"
        self.assertEqual(importer.prepare(response(mouse))[0].error.code, "unsupported_dependency")
        self.assertEqual(acquire("shadertoy", url).error.code, "provider_credentials")


if __name__ == "__main__":
    unittest.main()
