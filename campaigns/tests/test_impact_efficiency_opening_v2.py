import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BATCH = ROOT / "campaigns/sfm_stream_001/batches/impact_efficiency_opening_v2.json"
CANDIDATES = ROOT / "campaigns/sfm_stream_001/candidates/impact_efficiency"


class ImpactEfficiencyOpeningV2ContractTest(unittest.TestCase):
    def test_batch_declares_opening_readiness_contract(self):
        batch = json.loads(BATCH.read_text())
        self.assertEqual(batch["status"], "proposed")
        self.assertEqual(len(batch["candidate_ids"]), 12)
        for candidate_id in batch["candidate_ids"]:
            candidate = json.loads((CANDIDATES / (candidate_id + ".json")).read_text())
            self.assertEqual(candidate["batch_id"], batch["batch_id"])
            expected_unsupported = [92700000] if "absorption_divergence" not in candidate_id else []
            self.assertEqual(candidate["availability"]["unsupported_events"], expected_unsupported)
            self.assertEqual(candidate["availability"]["invalid_policy"], "unavailable")
            self.assertEqual(candidate["availability"]["readiness_policy"], "FactorEntry::GetReadinessMask(timestamp)")

    def test_versioned_contract_keeps_producer_factor_names(self):
        metadata = (ROOT / "base/hf-open5m-factor-demo/factors/impact_efficiency/meta_config.h").read_text()
        names = set(re.findall(r'"(impact_efficiency_[a-z0-9_]+)"', metadata))
        batch = json.loads(BATCH.read_text())
        for candidate_id in batch["candidate_ids"]:
            candidate = json.loads((CANDIDATES / (candidate_id + ".json")).read_text())
            self.assertIn(candidate["output"]["factor_name"], names)


if __name__ == "__main__":
    unittest.main()
