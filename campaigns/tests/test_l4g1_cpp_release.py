import json
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
APP_ROOT = ROOT / "base" / "hf-open5m-factor-demo"
ROUND = ROOT / "campaigns" / "sfm_stream_001" / "rounds" / "l4g1_mixed_v1.json"
FAMILIES = ["book_imbalance", "flow_pressure", "impact_efficiency", "liquidity_resilience"]


def compiled_metadata():
    source = textwrap.dedent(
        """
        #include <iostream>
        #include "factors/book_imbalance/meta_config.h"
        #include "factors/flow_pressure/meta_config.h"
        #include "factors/impact_efficiency/meta_config.h"
        #include "factors/liquidity_resilience/meta_config.h"
        void Emit(const factors::comm::FactorMetadata& m) {
          std::cout << m.factor_set_name << "\\t" << m.factor_size << "\\n";
          for (const auto& n : m.factor_names) std::cout << n << "\\n";
        }
        int main() {
          Emit(factors::book_imbalance::GetMetadata());
          Emit(factors::flow_pressure::GetMetadata());
          Emit(factors::impact_efficiency::GetMetadata());
          Emit(factors::liquidity_resilience::GetMetadata());
        }
        """
    )
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "probe.cc"
        binary = Path(td) / "probe"
        src.write_text(source)
        subprocess.run(["g++", "-std=c++11", "-I", str(APP_ROOT), str(src), "-o", str(binary)], check=True)
        lines = subprocess.check_output([str(binary)], text=True).splitlines()
    result = {}
    index = 0
    while index < len(lines):
        family, size = lines[index].split("\t")
        size = int(size)
        result[family] = lines[index + 1:index + 1 + size]
        index += size + 1
    return result


class L4G1CppReleaseTests(unittest.TestCase):
    def test_compiled_release_exposes_64_columns_with_four_new_candidates_per_family(self):
        metadata = compiled_metadata()
        self.assertEqual(set(metadata), set(FAMILIES))
        self.assertEqual(sum(len(names) for names in metadata.values()), 64)
        for family in FAMILIES:
            self.assertEqual(len(metadata[family]), 16)
            self.assertEqual(len(set(metadata[family])), 16)

        round_spec = json.loads(ROUND.read_text())
        self.assertEqual(round_spec["counts"]["candidate_count"], 16)
        for family in FAMILIES:
            names = [name for name in round_spec["candidate_ids"] if name.startswith(family + "_")]
            self.assertEqual(len(names), 4)
            for name in names:
                self.assertIn(name, metadata[family])


if __name__ == "__main__":
    unittest.main()
