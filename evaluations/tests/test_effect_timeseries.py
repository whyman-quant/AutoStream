import unittest
import pandas as pd

from evaluations.effect_timeseries import build_effect_timeseries


class EffectTimeseriesTests(unittest.TestCase):
    def test_cumulative_metrics_skip_not_ready(self):
        frame = pd.DataFrame([
            {"date":"20240102","event":"e1","universe":"000906","label":"raw926","ready":True,"rank_ic":.1,"long_return":.1, **{"q%d_return"%i:.01*i for i in range(1,11)}},
            {"date":"20240103","event":"e1","universe":"000906","label":"raw926","ready":False,"rank_ic":.9,"long_return":.9, **{"q%d_return"%i:.9 for i in range(1,11)}},
            {"date":"20240104","event":"e1","universe":"000906","label":"raw926","ready":True,"rank_ic":-.2,"long_return":-.1, **{"q%d_return"%i:.02 for i in range(1,11)}},
        ])
        out = build_effect_timeseries(frame, factor="f", selection_receipt={"selected_factors":["f"],"directions":{"f":"positive"},"best_events":{"000906|raw926":"e1"}})
        rows = out["rows"]
        self.assertEqual([r["ic_cumulative"] for r in rows], [.1, -.1])
        self.assertAlmostEqual(rows[-1]["long_cumulative"], (1.1*.9)-1)
        self.assertEqual(rows[-1]["coverage"], 2/3)
        self.assertEqual(rows[-1]["q10_cumulative"], (1+.1)*(1+.02)-1)

    def test_holdout_requires_authorized_frozen_selection(self):
        frame = pd.DataFrame([{"date":"20250102","event":"e1","universe":"000906","label":"raw926","ready":True,"rank_ic":.1,"long_return":.1}])
        with self.assertRaises(ValueError):
            build_effect_timeseries(frame, factor="f", selection_receipt=None)


if __name__ == "__main__":
    unittest.main()
