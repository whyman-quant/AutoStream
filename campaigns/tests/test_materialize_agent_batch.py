import json
import tempfile
import unittest
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash
from campaigns.materialize_agent_batch import materialize_agent_batch


class MaterializeAgentBatchTests(unittest.TestCase):
    def test_materializes_blueprint_as_l2_candidates_and_technical_batch(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            result = materialize_agent_batch(
                Path("campaigns/sfm_stream_002/logic/round_001.json"),
                Path("campaigns/sfm_stream_002/design/round_001_blueprint.json"),
                output,
                campaign_id="sfm_stream_002",
                batch_id="market_microstructure_round_001",
                source_commit="624eed9",
            )
            self.assertEqual(len(result["candidate_paths"]), 12)
            batch = json.loads(Path(result["batch_path"]).read_text())
            validate_document("batch", batch)
            self.assertEqual(batch["status"], "technical_complete")
            self.assertEqual(batch["candidate_ids"], result["candidate_ids"])
            for path in result["candidate_paths"]:
                candidate = json.loads(Path(path).read_text())
                validate_document("candidate", candidate)
                self.assertEqual(candidate["evidence_level"], "L2")
                self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
                self.assertEqual(candidate["availability"]["invalid_policy"], "unavailable")

    def test_can_advance_materialized_evidence_after_l3_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = materialize_agent_batch(
                Path("campaigns/sfm_stream_002/logic/round_001.json"),
                Path("campaigns/sfm_stream_002/design/round_001_blueprint.json"),
                Path(tmp), campaign_id="sfm_stream_002",
                batch_id="market_microstructure_round_001",
                source_commit="624eed9", evidence_level="L3",
                batch_status="pilot_complete",
            )
            batch = json.loads(Path(result["batch_path"]).read_text())
            candidate = json.loads(Path(result["candidate_paths"][0]).read_text())
            self.assertEqual(batch["status"], "pilot_complete")
            self.assertEqual(candidate["evidence_level"], "L3")


if __name__ == "__main__":
    unittest.main()
