import tempfile
import unittest
from pathlib import Path

from campaigns.sfm_stream_002.holdout import make_holdout_config


class MarketMicrostructureHoldoutTests(unittest.TestCase):
    def test_config_changes_only_output_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            target = root / "target.json"
            source.write_text(
                '{"factors_config":{"save_info":{"dir":"/old/[DATE]/all_families"},'
                '"factor_sets":[{"name":"market_microstructure","enabled":true}]}}',
                encoding="utf-8")
            result = make_holdout_config(source, target, root / "holdout")
            self.assertEqual(
                result["factors_config"]["save_info"]["dir"],
                str(root / "holdout" / "[DATE]" / "all_families"))
            self.assertEqual(result["factors_config"]["factor_sets"][0]["name"],
                             "market_microstructure")


if __name__ == "__main__":
    unittest.main()
