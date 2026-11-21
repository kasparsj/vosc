from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import sys
import unittest

PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR))

from adapter import validate_receipt  # noqa: E402


def metadata(phase: str = "ready") -> dict:
    return {
        "trial_id": "trial-1", "layer": 0, "phase": phase, "gpu_budget_ms": 8.0,
        "lease_remaining_ms": 5000, "evaluation_revision": 4, "first_frame": 10,
        "warmup_frames": 8, "minimum_samples": 30, "freshness_frames": 8,
        "context": {"revision": 4, "external_epoch": 1, "width": 1280, "height": 720},
        "samples": 30, "p95_ms": 3.0, "hard_max_ms": 4.0, "last_gpu_frame": 45,
    }


def receipt(status: str = "ready") -> dict:
    return {"id": "trial-1", "ok": True, "status": status, "revision": 4,
            "frame": 45, "external_epoch": 1, "method": "trial_shader",
            "applied_after_frame": 9, "result": {"trial": metadata(status)}}


class TrialReceiptValidationTests(unittest.TestCase):
    def test_ready_and_terminal_trial_receipts(self):
        self.assertEqual(validate_receipt(receipt(), "trial_shader", "trial-1")["status"], "ready")
        accepted = receipt("rendered")
        accepted["result"]["trial"]["phase"] = "accepted"
        accepted["result"]["trial"]["rendered_frame"] = 46
        accepted["frame"] = 46
        accepted["revision"] = 5
        validate_receipt(accepted, "trial_shader", "trial-1")

    def test_feedback_trial_requires_paired_coverage_and_composed_evidence(self):
        valid = receipt()
        valid["result"]["trial"].update(target="default_feedback_texture", candidate_program=17,
            measurement_id=3, pair_coverage=.95, candidate_draw_frame=45)
        validate_receipt(valid, "trial_shader", "trial-1")
        for key, value in (("pair_coverage", .949), ("candidate_draw_frame", 46)):
            invalid = deepcopy(valid)
            invalid["result"]["trial"][key] = value
            with self.assertRaises(ValueError):
                validate_receipt(invalid, "trial_shader", "trial-1")

    def test_feedback_action_draw_requires_matching_rendered_frame(self):
        action = {"id": "accept-1", "ok": True, "status": "rendered", "revision": 5,
                  "frame": 46, "external_epoch": 8, "method": "accept_trial", "applied_after_frame": 45,
                  "result": {"feedback_draw": {"trial_id": "trial-1", "evaluation_revision": 4,
                                               "frame": 46, "program": 17}}}
        validate_receipt(action, "accept_trial", "accept-1")
        for field, value in (("frame", 45), ("trial_id", "x" * 65)):
            invalid = deepcopy(action)
            invalid["result"]["feedback_draw"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_receipt(invalid, "accept_trial", "accept-1")

    def test_rejects_hostile_trial_metadata(self):
        invalid = []
        listed_phase = receipt()
        listed_phase["result"]["trial"]["phase"] = ["ready"]
        invalid.append(listed_phase)
        huge = receipt()
        huge["result"]["trial"]["lease_remaining_ms"] = 10 ** 400
        invalid.append(huge)
        unknown = receipt()
        unknown["result"]["trial"]["surprise"] = True
        invalid.append(unknown)
        bad_context = receipt()
        bad_context["result"]["trial"]["context"]["revision"] = "4"
        invalid.append(bad_context)
        wrong_context_revision = receipt()
        wrong_context_revision["result"]["trial"]["context"]["revision"] = 5
        invalid.append(wrong_context_revision)
        wrong_id = receipt()
        wrong_id["result"]["trial"]["trial_id"] = "different"
        invalid.append(wrong_id)
        expired_ready = receipt()
        expired_ready["result"]["trial"]["lease_remaining_ms"] = 0
        invalid.append(expired_ready)
        over_budget = receipt()
        over_budget["result"]["trial"]["hard_max_ms"] = 16.01
        invalid.append(over_budget)
        missing_evidence = receipt()
        missing_evidence["result"]["trial"].update(samples=0, p95_ms=None,
                                                      hard_max_ms=None, last_gpu_frame=None)
        invalid.append(missing_evidence)
        rollback_list = receipt("rolling_back")
        rollback_list["result"]["trial"]["rollback_code"] = []
        invalid.append(rollback_list)
        future_revision = receipt()
        future_revision["result"]["trial"]["evaluation_revision"] = 5
        future_revision["result"]["trial"]["context"]["revision"] = 5
        invalid.append(future_revision)
        zero_ack = receipt("rendered")
        zero_ack["result"]["trial"].update(phase="accepted", rendered_frame=0)
        invalid.append(zero_ack)
        early_ack = receipt("rendered")
        early_ack["result"]["trial"].update(phase="accepted", rendered_frame=9)
        invalid.append(early_ack)
        for candidate in invalid:
            with self.assertRaises((ValueError, TypeError, OverflowError)):
                validate_receipt(candidate, "trial_shader", "trial-1")

    def test_failed_rollback_requires_valid_trial_metadata(self):
        failed = receipt("rolling_back")
        failed.update(ok=False, status="failed",
                      error={"code": "trial_cancelled", "message": "rolled back", "diagnostics": []})
        failed["result"]["trial"].update(phase="rolled_back", rollback_code="trial_cancelled",
                                            restored_previous=True, restoration_revision=5,
                                            rendered_frame=46)
        failed["frame"] = 46
        failed["revision"] = 5
        validate_receipt(failed, "trial_shader", "trial-1")
        malformed = deepcopy(failed)
        malformed["result"]["trial"]["restored_previous"] = 1
        with self.assertRaises(ValueError):
            validate_receipt(malformed, "trial_shader", "trial-1")
        for impossible in (3, 6):
            malformed = deepcopy(failed)
            malformed["result"]["trial"]["restoration_revision"] = impossible
            with self.assertRaises(ValueError):
                validate_receipt(malformed, "trial_shader", "trial-1")


if __name__ == "__main__":
    unittest.main()
