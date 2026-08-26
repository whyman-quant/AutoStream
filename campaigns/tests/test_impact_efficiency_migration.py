import json
import re
import unittest
from pathlib import Path
from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash, validate_idea_candidate_batch
ROOT=Path(__file__).parents[2]
CAMPAIGN=ROOT/"campaigns"/"sfm_stream_001"
IDEA_PATH=CAMPAIGN/"ideas"/"impact_efficiency.json"
BATCH_PATH=CAMPAIGN/"batches"/"impact_efficiency_seed_v1.json"
CANDIDATE_PATH=CAMPAIGN/"candidates"/"impact_efficiency"/"impact_efficiency_signed_price_impact_w16.json"
METADATA_PATH=ROOT/"base"/"hf-open5m-factor-demo"/"factors"/"impact_efficiency"/"meta_config.h"
def load_json(path): return json.loads(path.read_text(encoding="utf-8"))
class ImpactEfficiencyMigrationTests(unittest.TestCase):
    def test_representative_artifacts_are_consistent(self):
        idea,batch,candidate=load_json(IDEA_PATH),load_json(BATCH_PATH),load_json(CANDIDATE_PATH)
        for kind,doc in (("idea_spec",idea),("batch",batch),("candidate",candidate)): validate_document(kind,doc)
        self.assertEqual(idea["representative_candidate_id"],candidate["candidate_id"])
        self.assertIn(candidate["candidate_id"], batch["candidate_ids"])
        self.assertEqual(candidate["operator_id"],"signed_price_impact_efficiency")
        self.assertEqual(candidate["canonical_hash"],candidate_hash(candidate))
        candidate_paths = [CAMPAIGN / "candidates" / "impact_efficiency" / (candidate_id + ".json") for candidate_id in batch["candidate_ids"]]
        validate_idea_candidate_batch(candidate_paths,BATCH_PATH,IDEA_PATH)
    def test_seed_batch_contains_twelve_contract_candidates(self):
        batch = load_json(BATCH_PATH)
        self.assertEqual(len(batch["candidate_ids"]), 12)
        candidate_paths = [CAMPAIGN / "candidates" / "impact_efficiency" / (candidate_id + ".json") for candidate_id in batch["candidate_ids"]]
        for path in candidate_paths:
            self.assertTrue(path.exists(), path)
            candidate = load_json(path)
            validate_document("candidate", candidate)
            self.assertEqual(candidate["batch_id"], batch["batch_id"])
            self.assertEqual(candidate["family_id"], "impact_efficiency")
            self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
        validate_idea_candidate_batch(candidate_paths, BATCH_PATH, IDEA_PATH)
        evidence = {load_json(path)["candidate_id"]: load_json(path)["evidence_level"] for path in candidate_paths}
        self.assertEqual(evidence["impact_efficiency_signed_price_impact_w16"], "L1")
        self.assertEqual({level for candidate_id, level in evidence.items() if candidate_id != "impact_efficiency_signed_price_impact_w16"}, {"L0"})
    def test_candidate_name_matches_cpp_metadata(self):
        batch=load_json(BATCH_PATH); text=METADATA_PATH.read_text(encoding="utf-8"); block=re.search(r"kFactorNames\s*=\s*\{(.*?)\};",text,re.S); self.assertIsNotNone(block); self.assertEqual(re.findall(r'"([^"]+)"',block.group(1)),batch["candidate_ids"])
