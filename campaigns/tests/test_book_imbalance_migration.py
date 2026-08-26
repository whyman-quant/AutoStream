import json
import hashlib
import re
import tempfile
import unittest
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash, validate_candidate_batch


ROOT = Path(__file__).parents[2]
CAMPAIGN = ROOT / "campaigns" / "sfm_stream_001"
CANDIDATE_DIR = CAMPAIGN / "candidates" / "book_imbalance"
BATCH_PATH = CAMPAIGN / "batches" / "book_imbalance_seed_v1.json"
IDEA_PATH = CAMPAIGN / "ideas" / "book_imbalance.json"
METADATA_PATH = ROOT / "base" / "hf-open5m-factor-demo" / "factors" / "book_imbalance" / "meta_config.h"
PORTRAIT_DIR = CAMPAIGN / "portraits" / "pilot" / "book_imbalance"
RECEIPT_PATH = CAMPAIGN / "manifests" / "multi-day-observation-20251009-20251031.json"

OPERATIONS = [1, 0, 2, 3, 1, 0, 2, 3, 1, 0, 2, 3]


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def metadata_factor_names():
    text = METADATA_PATH.read_text(encoding="utf-8")
    block = re.search(r"kFactorNames\s*=\s*\{(.*?)\};", text, re.S)
    if block is None:
        raise AssertionError("kFactorNames not found")
    return re.findall(r'"([^"]+)"', block.group(1))


class BookImbalanceMigrationTests(unittest.TestCase):
    def setUp(self):
        self.paths = sorted(CANDIDATE_DIR.glob("*.json"))

    def test_twelve_candidates_match_idea_and_cpp_metadata(self):
        self.assertEqual(len(self.paths), 12)
        candidates = [load_json(path) for path in self.paths]
        for document in candidates:
            validate_document("candidate", document)
            self.assertEqual(document["output"]["factor_name"], document["candidate_id"])
            self.assertEqual(document["availability"]["lag_events"], 0)

        cpp_names = metadata_factor_names()
        self.assertEqual({value["candidate_id"] for value in candidates}, set(cpp_names))

        idea = load_json(IDEA_PATH)
        expected = {
            item["template_id"]: (
                item["operator_id"], int(item["window"].split("_")[0]),
                item["variant"], item["lag_events"],
            )
            for item in idea["template_grid"]
        }
        for index, item in enumerate(idea["template_grid"]):
            candidate = next(value for value in candidates if value["candidate_id"] == cpp_names[index])
            actual = (
                candidate["operator_id"], candidate["parameters"]["window_events"],
                candidate["parameters"]["variant"], candidate["parameters"]["lag_events"],
            )
            self.assertEqual(actual, expected[item["template_id"]])
            self.assertEqual(candidate["parameters"]["operation"], OPERATIONS[index])

    def test_candidate_hashes_are_unique_and_recomputable(self):
        candidates = [load_json(path) for path in self.paths]
        hashes = [candidate_hash(document) for document in candidates]
        self.assertEqual(len(hashes), 12)
        self.assertEqual(len(set(hashes)), 12)
        for document, actual_hash in zip(candidates, hashes):
            self.assertEqual(document["canonical_hash"], actual_hash)

    def test_batch_lists_exact_candidates_and_cross_artifacts_are_consistent(self):
        batch = load_json(BATCH_PATH)
        validate_document("batch", batch)
        self.assertEqual(batch["search_policy"]["candidate_budget"], 12)
        self.assertEqual(set(batch["candidate_ids"]), {path.stem for path in self.paths})
        validate_candidate_batch(self.paths, BATCH_PATH, IDEA_PATH, METADATA_PATH)

    def test_cross_artifact_check_rejects_batch_budget_mismatch(self):
        batch = load_json(BATCH_PATH)
        batch["search_policy"]["candidate_budget"] = 13
        with tempfile.TemporaryDirectory() as directory:
            invalid_batch = Path(directory) / "batch.json"
            invalid_batch.write_text(json.dumps(batch), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "budget"):
                validate_candidate_batch(self.paths, invalid_batch, IDEA_PATH, METADATA_PATH)

    def test_one_l3_portrait_per_candidate_matches_signed_pilot_receipt(self):
        portrait_paths = sorted(PORTRAIT_DIR.glob("*.json"))
        self.assertEqual(len(portrait_paths), 12)
        receipt = load_json(RECEIPT_PATH)
        receipt_hash = "sha256:" + hashlib.sha256(RECEIPT_PATH.read_bytes()).hexdigest()
        candidates = {path.stem: load_json(path) for path in self.paths}
        portraits = [load_json(path) for path in portrait_paths]
        self.assertEqual({item["candidate_id"] for item in portraits}, set(candidates))

        for portrait in portraits:
            validate_document("factor_portrait", portrait)
            candidate_id = portrait["candidate_id"]
            self.assertEqual(portrait["scope"], "pilot")
            self.assertEqual(portrait["evidence_level"], "L3")
            self.assertEqual(portrait["dataset"]["date_count"], 17)
            self.assertEqual(len(portrait["dataset"]["events"]), 8)
            self.assertEqual(set(portrait["dataset"]["labels"]), {"raw926", "ease926"})
            self.assertEqual(set(portrait["dataset"]["universes"]), {"000985", "003800", "000906"})
            self.assertEqual(portrait["lineage"]["candidate_hash"], candidates[candidate_id]["canonical_hash"])
            self.assertEqual(portrait["lineage"]["receipt_sha256"], receipt_hash)
            self.assertFalse(portrait["decision"]["promotion_allowed"])
            self.assertEqual(portrait["decision"]["status"], "observation_only")

            slices = {(item["label"], item["universe"]): item for item in portrait["metric_slices"]}
            self.assertEqual(len(slices), 6)
            for combination, factors in receipt["combinations"].items():
                label, universe = combination.split(":")
                expected = factors[candidate_id]
                actual = slices[(label, universe)]
                self.assertEqual(actual["observation_count"], expected["observation_count"])
                self.assertEqual(actual["metrics"], {key: expected[key] for key in actual["metrics"]})


if __name__ == "__main__":
    unittest.main()
