import unittest

from evaluations.round_experience import allocate_wide_search


class RoundExperienceTests(unittest.TestCase):
    def test_normal_round_allocates_sixteen_structures_per_mechanism(self):
        allocation = allocate_wide_search(["a", "b", "c", "d", "e", "f"])
        self.assertEqual(sum(allocation.values()), 96)
        self.assertEqual(set(allocation.values()), {16})


if __name__ == "__main__":
    unittest.main()
