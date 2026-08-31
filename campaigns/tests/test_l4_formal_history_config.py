import json
import subprocess
import tempfile
import textwrap
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


def load_compiled_metadata():
    probe_source = textwrap.dedent(
        """
        #include <iostream>
        #include <string>
        #include "factors/book_imbalance/meta_config.h"
        #include "factors/flow_pressure/meta_config.h"
        #include "factors/liquidity_resilience/meta_config.h"
        #include "factors/impact_efficiency/meta_config.h"

        void Emit(const factors::comm::FactorMetadata& metadata) {
          std::cout << "F\\t" << metadata.factor_set_name << "\\t"
                    << metadata.factor_size << "\\t"
                    << metadata.factor_names.size() << "\\n";
          for (const std::string& name : metadata.factor_names) {
            std::cout << "N\\t" << name.size() << "\\t" << name << "\\n";
          }
        }

        int main() {
          Emit(factors::book_imbalance::GetMetadata());
          Emit(factors::flow_pressure::GetMetadata());
          Emit(factors::liquidity_resilience::GetMetadata());
          Emit(factors::impact_efficiency::GetMetadata());
          return 0;
        }
        """
    )
    with tempfile.TemporaryDirectory() as directory:
        directory_path = Path(directory)
        source_path = directory_path / "metadata_probe.cc"
        binary_path = directory_path / "metadata_probe"
        source_path.write_text(probe_source, encoding="utf-8")
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-I",
                str(APP_ROOT),
                str(source_path),
                "-o",
                str(binary_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        output = subprocess.run(
            [str(binary_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout

    metadata = {}
    current = None
    for line in output.splitlines():
        record_type, first, second, *rest = line.split("\t", 3)
        if record_type == "F":
            current = first
            metadata[current] = {
                "factor_size": int(second),
                "expected_name_count": int(rest[0]),
                "factor_names": [],
            }
        elif record_type == "N":
            if current is None:
                raise AssertionError("factor name appeared before family metadata")
            name = second
            if len(name.encode("utf-8")) != int(first):
                raise AssertionError(f"invalid length-prefixed factor name: {name}")
            metadata[current]["factor_names"].append(name)
        else:
            raise AssertionError(f"unknown metadata probe record: {line}")

    for family, values in metadata.items():
        if values["expected_name_count"] != len(values["factor_names"]):
            raise AssertionError(f"truncated metadata probe output for {family}")
    return metadata


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
        compiled_metadata = load_compiled_metadata()
        compiled_names = []
        total_factor_size = 0
        for family in configured_families:
            batch_names = load_json(
                CAMPAIGN / "batches" / f"{family}_seed_v1.json"
            )["candidate_ids"]
            family_metadata = compiled_metadata[family]
            self.assertEqual(
                family_metadata["factor_size"],
                len(batch_names),
            )
            self.assertEqual(
                family_metadata["factor_size"],
                len(family_metadata["factor_names"]),
            )
            total_factor_size += family_metadata["factor_size"]
            compiled_names.extend(family_metadata["factor_names"])
        self.assertEqual(total_factor_size, 48)
        self.assertEqual(compiled_names, load_batch_factor_names())

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
        self.assertEqual(
            factor_config["send_times"],
            {"start": 93000, "end": 150000, "interval": 1800, "add": [92700]},
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
