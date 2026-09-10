import unittest
import pandas as pd
import matplotlib
matplotlib.use("Agg")
from evaluations.effect_plot import plot_effect_grid


class EffectPlotTests(unittest.TestCase):
    def test_fixed_6x3_layout(self):
        rows=[]
        for u in ("000906","003800","000985"):
            for label in ("raw926","ease926"):
                for e in range(8):
                    rows.append({"date":"20240102","event":str(e),"universe":u,"label":label,"ready":True,"ic_cumulative":.1,"long_cumulative":.2, **{"q%d_cumulative"%q:.01*q for q in range(1,11)}})
        fig = plot_effect_grid(pd.DataFrame(rows), factor="f", best_events={u+"|"+l:"0" for u in ("000906","003800","000985") for l in ("raw926","ease926")})
        self.assertEqual(len(fig.axes), 18)
        self.assertEqual([ax.get_ylabel() for ax in fig.axes[::3]], ["000906 · raw926","000906 · ease926","003800 · raw926","003800 · ease926","000985 · raw926","000985 · ease926"])
        self.assertTrue(any("holdout" in t.get_text().lower() for ax in fig.axes for t in ax.texts))


if __name__ == "__main__":
    unittest.main()
