import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
CAMPAIGN = ROOT / "campaigns" / "sfm_stream_001"
APP_ROOT = ROOT / "base" / "hf-open5m-factor-demo"
CONFIG_PATH = APP_ROOT / "config_factor_sfm_stream_001_formal_history_v2.json"
PILOT_CONFIG_PATH = APP_ROOT / "config_factor_sfm_stream_001_flow_pressure_pilot.json"
FAMILIES = [
    "book_imbalance",
    "flow_pressure",
    "liquidity_resilience",
    "impact_efficiency",
]
SAVE_TIMES = [92700, 100000, 103000, 110000, 113000, 133000, 140000, 143000]


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def load_batch_factor_names():
    names = []
    for family in FAMILIES:
        batch = load_json(CAMPAIGN / "batches" / f"{family}_seed_v1.json")
        names.extend(batch["candidate_ids"])
    return names


def load_cpp_factor_names(family):
    metadata_path = APP_ROOT / "factors" / family / "meta_config.h"
    text = metadata_path.read_text(encoding="utf-8")
    declaration = re.search(r"\bkFactorNames\s*=\s*\{", text)
    if declaration is None:
        raise AssertionError(f"kFactorNames initializer not found in {metadata_path}")

    opening_brace = text.find("{", declaration.start())
    depth = 0
    closing_brace = None
    for index in range(opening_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                closing_brace = index
                break
    if closing_brace is None:
        raise AssertionError(f"unterminated kFactorNames initializer in {metadata_path}")

    initializer = text[opening_brace + 1:closing_brace]
    return re.findall(r'"((?:\\.|[^"\\])*)"', initializer)


class L4FormalHistoryConfigTests(unittest.TestCase):
    def load_config(self):
        self.assertTrue(CONFIG_PATH.is_file(), f"missing frozen config: {CONFIG_PATH}")
        return load_json(CONFIG_PATH)

    def test_batches_freeze_48_globally_unique_factor_names(self):
        names = load_batch_factor_names()
        self.assertEqual(len(names), 48)
        self.assertEqual(len(set(names)), 48)

    def test_config_enables_only_the_four_families_in_hdf5_column_order(self):
        config = self.load_config()
        factor_sets = config["factors_config"]["factor_sets"]
        self.assertEqual(
            factor_sets,
            [{"name": family, "enabled": True} for family in FAMILIES],
        )

        configured_families = [item["name"] for item in factor_sets]
        cpp_names = []
        for family in configured_families:
            cpp_names.extend(load_cpp_factor_names(family))
        self.assertEqual(cpp_names, load_batch_factor_names())

    def test_config_uses_the_frozen_market_inputs_and_pilot_send_times(self):
        config = self.load_config()
        factor_config = config["factors_config"]
        pilot_factor_config = load_json(PILOT_CONFIG_PATH)["factors_config"]
        self.assertEqual(
            factor_config["ev"],
            "/mnt/beegfs_ssd_raid91/706_wgh_new/stock_open/basedata",
        )
        self.assertEqual(
            factor_config["ev_code_file"],
            "[DATE]/per1day/lab200005_codelist.h5",
        )
        self.assertEqual(factor_config["send_times"], pilot_factor_config["send_times"])

    def test_config_freezes_output_times_and_runtime_controls(self):
        config = self.load_config()
        factor_config = config["factors_config"]
        self.assertEqual(factor_config["save_info"], {
            "dir": "/home/fangwei/mnt-ssd/AutoStream/data/sfm_autoresearch_001/formal-history-v2/[DATE]/all_families",
            "save_stats": True,
            "save_times": SAVE_TIMES,
        })
        self.assertEqual(factor_config["thread_num"], 8)
        self.assertIs(config["skip_missed_time_triggers"], False)


if __name__ == "__main__":
    unittest.main()
