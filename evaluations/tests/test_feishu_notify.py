import json
import base64
import hashlib
import hmac
import time
import unittest
from unittest import mock

from evaluations.feishu_notify import FeishuNotifier, build_round_start, build_round_end


class FeishuNotifyTests(unittest.TestCase):
    def test_round_start_message_uses_preregistered_research_language(self):
        research_round = {
            "date_lists": {"training": {"date_start": "20210104", "date_end": "20241231"},
                           "observation": {"date_start": "20210104", "date_end": "20241231"}},
            "families": {
                "book_imbalance": {
                    "research_question": "Does book displacement persist?",
                    "single_variable_control": "window only",
                    "parent_candidate_ids": ["parent_book"],
                    "success_condition": "replicates across splits",
                    "rejection_condition": "no parent increment",
                    "stop_condition": "lookahead stops the round",
                },
                "flow_pressure": {
                    "research_question": "Does flow decay help?", "single_variable_control": "half-life only",
                    "parent_candidate_ids": ["parent_flow"], "success_condition": "replicates",
                    "rejection_condition": "no increment", "stop_condition": "future trade stops",
                },
            },
            "selection": {"forbidden_years": [2025]},
        }
        payload = build_round_start(
            "l4g1", "mixed research", ["L2", "L3", "L4"], research_round=research_round,
        )
        self.assertEqual(payload["msg_type"], "text")
        text = payload["content"]["text"]
        self.assertIn("l4g1", text)
        self.assertIn("Does book displacement persist?", text)
        self.assertIn("window only", text)
        self.assertIn("parent_book", text)
        self.assertIn("20210104", text)
        self.assertIn("replicates across splits", text)
        self.assertIn("no parent increment", text)
        self.assertIn("lookahead stops the round", text)
        self.assertIn("2025 holdout: sealed", text)

    def test_round_end_message_reports_unit_evidence_ranges_actions_and_charts(self):
        report = {
            "status_counts": {"supported": 2, "promising": 1, "unsupported": 3,
                              "not_evaluable": 4, "error": 0},
            "supported_cells": [{"factor": "f1", "event": 100000000,
                                  "universe": "000906", "label": "raw926",
                                  "parent_delta": 0.012}],
            "next_actions": ["retain f1 for a constrained confirmation"],
            "promotion_allowed": False,
        }
        payload = build_round_end(
            "l4g1", report, plots=["/tmp/f1.png"],
        )
        text = payload["content"]["text"]
        self.assertIn("supported=2", text)
        self.assertIn("not_evaluable=4", text)
        self.assertIn("f1", text)
        self.assertIn("100000000", text)
        self.assertIn("000906", text)
        self.assertIn("parent_delta=0.012", text)
        self.assertIn("retain f1", text)
        self.assertIn("/tmp/f1.png", text)
        self.assertIn("promotion_allowed=false", text)
        self.assertNotIn("有效因子", text)
        self.assertNotIn("pass cell", text)

    def test_send_uses_signed_webhook_and_returns_response(self):
        notifier = FeishuNotifier(
            "https://example.invalid/hook", secret="test-secret", opener=mock.Mock(),
            clock=lambda: 1700000000,
        )
        response = mock.Mock()
        response.read.return_value = b'{"code":0}'
        notifier.opener.open.return_value = response
        result = notifier.send({"msg_type": "text", "content": {"text": "hello"}})
        request = notifier.opener.open.call_args.args[0]
        self.assertEqual(request.full_url, "https://example.invalid/hook")
        body = json.loads(request.data)
        self.assertEqual(body["msg_type"], "text")
        self.assertEqual(body["timestamp"], "1700000000")
        expected = base64.b64encode(
            hmac.new(b"test-secret", b"1700000000\n" + b"test-secret", hashlib.sha256).digest()
        ).decode()
        self.assertEqual(body["sign"], expected)
        self.assertEqual(result["code"], 0)


if __name__ == "__main__":
    unittest.main()
