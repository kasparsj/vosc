"""Native wait semantics: ready permits explicit acceptance, not automatic commit."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("vosc_control_client", Path(__file__).resolve().parents[1] / "scripts/control_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class WaitTests(unittest.TestCase):
    def wait_for(self, statuses):
        client = module.Client.__new__(module.Client)
        replies = iter({"ok": True, "result": {"id": "trial", "status": state}} for state in statuses)
        calls = []
        def call(method, params):
            calls.append((method, params))
            return next(replies)
        client.call = call
        with patch.object(module.time, "sleep"):
            result = client.wait("trial")
        self.assertTrue(all(method == "get_request" and params == {"request_id": "trial"} for method, params in calls))
        return result, len(calls)

    def test_trial_wait_returns_ready_without_accepting(self):
        result, calls = self.wait_for(["evaluating", "evaluating", "ready"])
        self.assertEqual(result["status"], "ready")
        self.assertEqual(calls, 3)

    def test_accept_and_rollback_wait_for_draw_result(self):
        result, calls = self.wait_for(["committing", "rendered"])
        self.assertEqual((result["status"], calls), ("rendered", 2))
        result, calls = self.wait_for(["rolling_back", "failed"])
        self.assertEqual((result["status"], calls), ("failed", 2))

    def test_original_mutation_wait_is_preserved(self):
        result, calls = self.wait_for(["queued", "applied", "rendered"])
        self.assertEqual((result["status"], calls), ("rendered", 3))

    def test_missing_receipt_returns_query_failure(self):
        client = module.Client.__new__(module.Client)
        client.call = lambda method, params: {"ok": False, "status": "failed", "error": {"code": "request_not_found"}}
        self.assertEqual(client.wait("missing")["error"]["code"], "request_not_found")


if __name__ == "__main__":
    unittest.main()
