import json
import tempfile
import unittest
from pathlib import Path

from campaigns.release_factor_manifest import build_factor_manifest, load_factor_manifest


class ReleaseFactorManifestTests(unittest.TestCase):
    def _write_batch(self, root, name, ids, family):
        path = Path(root) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({"batch_id": path.stem, "family_id": family,
                                    "candidate_ids": ids}), encoding="utf-8")
        return path

    def test_derives_order_and_counts_from_batches_not_constants(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            batches = [
                self._write_batch(root, "book_seed.json", ["book_a", "book_b"], "book"),
                self._write_batch(root, "book_next.json", ["book_c"], "book"),
                self._write_batch(root, "flow_seed.json", ["flow_a"], "flow"),
                self._write_batch(root, "flow_next.json", ["flow_b", "flow_c"], "flow"),
            ]
            document = build_factor_manifest("release-x", batches)
            self.assertEqual(document["factor_count"], 6)
            self.assertEqual(document["factor_names"], ["book_a", "book_b", "book_c", "flow_a", "flow_b", "flow_c"])
            self.assertEqual(document["families"][0]["control_count"], 2)
            self.assertEqual(document["families"][0]["candidate_count"], 1)

    def test_rejects_duplicate_factor_across_batches(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            batches = [
                self._write_batch(root, "a.json", ["same"], "book"),
                self._write_batch(root, "b.json", ["same"], "flow"),
            ]
            with self.assertRaisesRegex(ValueError, "duplicate"):
                build_factor_manifest("release-x", batches)

    def test_load_recomputes_manifest_hash_and_rejects_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            batch = self._write_batch(root, "a.json", ["f1"], "book")
            manifest = build_factor_manifest("release-x", [batch])
            path = root / "factor-manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(load_factor_manifest(path)["factor_names"], ["f1"])
            payload = json.loads(path.read_text())
            payload["factor_names"].append("tampered")
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash"):
                load_factor_manifest(path)


if __name__ == "__main__":
    unittest.main()
