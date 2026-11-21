import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adapter import validate_query
from scene_recipe_schema import RECIPE_LIMITS, RecipeValidationError, validate_export_reply, validate_recipe_capabilities


def recipe():
    return {"format": "vosc.osc-recipe", "version": 1,
            "resolution": {"width": 800, "height": 600}, "onset_gate": 0,
            "commands": [{"address": "/layers/reset", "args": [
                {"type": "i", "value": 0}, {"type": "s", "value": "stack"}]}],
            "assets": []}


def reply(value):
    return {"id": "q", "ok": True, "status": "ok", "revision": 0,
            "frame": 0, "external_epoch": 0, "result": {"recipe": value}}


class RecipeAdapterTests(unittest.TestCase):
    def test_export_reply_is_validated(self):
        self.assertEqual(validate_query(reply(recipe()), "export_scene")["status"], "ok")

    def test_hostile_export_reply_rejects_before_consumer(self):
        value = recipe(); value["commands"][0]["args"][0]["value"] = True
        with self.assertRaises((ValueError, RecipeValidationError)):
            validate_query(reply(value), "export_scene")

    def test_export_envelope_rejects_truthy_ok_and_missing_metadata(self):
        malformed = []
        truthy = reply(recipe()); truthy["ok"] = "yes"; malformed.append(truthy)
        for field in ("status", "revision", "frame", "external_epoch"):
            missing = reply(recipe()); missing.pop(field); malformed.append(missing)
        extra_result = reply(recipe()); extra_result["result"]["extra"] = True; malformed.append(extra_result)
        for value in malformed:
            with self.subTest(value=value), self.assertRaises(RecipeValidationError):
                validate_export_reply(value)

    def test_export_failure_preserves_strict_native_diagnostics(self):
        failed = {"id": "q", "ok": False, "status": "failed", "revision": 3,
                  "frame": 4, "external_epoch": 5,
                  "error": {"code": "unsupported_scene",
                            "message": "layer 2 field texture reason unsupported looper",
                            "diagnostics": [{"layer": 2, "field": "texture", "reason": "looper"}]}}
        self.assertIs(validate_export_reply(failed), failed)
        self.assertIs(validate_query(failed, "export_scene"), failed)
        malformed = dict(failed); malformed["error"] = {**failed["error"], "extra": True}
        with self.assertRaises(RecipeValidationError):
            validate_export_reply(malformed)

    def test_recipe_capabilities_are_conditional_and_frozen(self):
        baseline = {"operations": ["get_capabilities", "get_scene"], "features": {}, "limits": {}}
        validate_recipe_capabilities(baseline)
        advertised = {"operations": ["get_capabilities", "export_scene"],
                      "features": {"scene_recipe_export": True}, "limits": dict(RECIPE_LIMITS)}
        validate_recipe_capabilities(advertised)
        for mutation in ("feature", "limit", "orphan"):
            value = {"operations": list(advertised["operations"]),
                     "features": dict(advertised["features"]), "limits": dict(advertised["limits"])}
            if mutation == "feature": value["features"]["scene_recipe_export"] = 1
            if mutation == "limit": value["limits"]["recipe_bundle_bytes"] += 1
            if mutation == "orphan": value["operations"].remove("export_scene")
            with self.subTest(mutation=mutation), self.assertRaises(RecipeValidationError):
                validate_recipe_capabilities(value)


if __name__ == "__main__":
    unittest.main()
