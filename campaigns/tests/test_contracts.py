import copy
import unittest

from campaigns.contracts import validate_document


SHA256 = "sha256:" + "a" * 64


def candidate_document():
    return {
        "schema_version": 1,
        "kind": "candidate",
        "candidate_id": "book_imbalance_weighted_depth_imbalance_w16_v0_lag0",
        "campaign_id": "sfm_stream_001",
        "family_id": "book_imbalance",
        "batch_id": "book_imbalance_seed_v1",
        "generation": 0,
        "parent_candidate_ids": [],
        "hypothesis_id": "standing_depth_predicts_near_term_price_pressure",
        "operator_id": "weighted_depth_imbalance",
        "formula": "prepare(depth_imbalance(levels=2), variant=0, lag=0) - normalizer(16)",
        "source_streams": ["quote"],
        "parameters": {
            "window_events": 16,
            "variant": 0,
            "lag_events": 0,
            "operation": 1,
        },
        "state": {
            "window_type": "event_count",
            "window_events": 16,
            "warmup_events": 0,
            "reset_policy": "trading_day",
        },
        "availability": {
            "update_on": ["quote"],
            "output_at": "scheduled_snapshot",
            "lag_events": 0,
            "invalid_policy": "finite_or_zero",
        },
        "output": {
            "factor_name": "book_imbalance_weighted_depth_imbalance_w16_v0_lag0",
            "dtype": "float64",
            "research_direction": "raw_signed",
        },
        "lineage": {
            "idea_path": "campaigns/sfm_stream_001/ideas/book_imbalance.json",
            "implementation_path": "base/hf-open5m-factor-demo/factors/book_imbalance/factor_entry.cpp",
            "source_commit": "b455935",
        },
        "evidence_level": "L3",
        "canonical_hash": SHA256,
    }


def batch_document():
    return {
        "schema_version": 1,
        "kind": "batch",
        "batch_id": "book_imbalance_seed_v1",
        "campaign_id": "sfm_stream_001",
        "family_id": "book_imbalance",
        "generation": 0,
        "parent_batch_ids": [],
        "parent_experience_ids": [],
        "hypothesis_id": "standing_depth_predicts_near_term_price_pressure",
        "objective": "Establish signed quote-book pressure seeds.",
        "change_dimension": "seed_grid",
        "candidate_ids": ["candidate_a", "candidate_b"],
        "search_policy": {"method": "mechanism_grid", "candidate_budget": 2},
        "created_at": "2026-08-26",
        "source_commit": "b455935",
        "status": "pilot_complete",
    }


def portrait_document():
    return {
        "schema_version": 1,
        "kind": "factor_portrait",
        "portrait_id": "book_factor__pilot_20251009_20251031",
        "campaign_id": "sfm_stream_001",
        "family_id": "book_imbalance",
        "candidate_id": "book_factor",
        "scope": "pilot",
        "evidence_level": "L3",
        "dataset": {
            "dataset_id": "pilot_20251009_20251031",
            "date_start": "20251009",
            "date_end": "20251031",
            "date_count": 17,
            "events": [92600000, 100000000],
            "labels": ["raw926", "ease926"],
            "universes": ["000985", "003800", "000906"],
        },
        "lineage": {
            "candidate_hash": SHA256,
            "source_commit": "b455935",
            "evaluator_version": "factor_eval_toolkit@local",
            "receipt_path": "campaigns/sfm_stream_001/manifests/observation.json",
            "receipt_sha256": SHA256,
        },
        "data_quality": {
            "all_required_values_finite": True,
            "duplicate_key_count": 0,
            "coverage_status": "complete",
        },
        "metric_slices": [{
            "label": "raw926",
            "universe": "000985",
            "observation_count": 136,
            "metrics": {
                "mean_rank_ic": -0.01,
                "rank_ic_positive_fraction": 0.4,
                "mean_ic": -0.02,
                "ic_positive_fraction": 0.3,
                "mean_long_short": -0.001,
                "long_short_positive_fraction": 0.4,
                "mean_monotonicity": 0.1,
            },
        }],
        "direction": {"policy": "raw_signed", "frozen": False},
        "redundancy": {
            "status": "not_measured_in_receipt",
            "max_abs_spearman": None,
            "peer_candidate_id": None,
        },
        "decision": {
            "status": "observation_only",
            "promotion_allowed": False,
            "reasons": ["pilot_is_not_formal_history"],
        },
    }


def experience_document():
    return {
        "schema_version": 1,
        "kind": "experience_record",
        "experience_id": "experience_book_imbalance_001",
        "campaign_id": "sfm_stream_001",
        "family_id": "book_imbalance",
        "source_portrait_ids": ["portrait_001"],
        "fact": {
            "statement": "RankIC weakens at the first event.",
            "evidence": [{
                "portrait_id": "portrait_001",
                "metric_path": "event_slices.92600000.mean_rank_ic",
                "observed_value": -0.02,
            }],
        },
        "interpretation": {
            "statement": "The opening snapshot may need a shorter state window.",
            "falsifiable_by": "Compare adjacent shorter windows on formal history.",
        },
        "action": {
            "type": "generate_variants",
            "changed_dimension": "window_events",
            "target_family_id": "book_imbalance",
            "proposal": "Generate adjacent shorter event windows.",
            "falsifiable_test": {
                "expected_observation": "Opening-event stability improves without losing later events.",
                "reject_condition": "Opening stability does not improve on validation dates.",
                "dataset_scope": "next_formal_history_batch",
            },
        },
        "status": "proposed",
        "generated_batch_ids": [],
    }


class ContractTests(unittest.TestCase):
    def test_accepts_valid_documents(self):
        for kind, document in (
            ("candidate", candidate_document()),
            ("batch", batch_document()),
            ("factor_portrait", portrait_document()),
            ("experience_record", experience_document()),
        ):
            with self.subTest(kind=kind):
                validate_document(kind, document)

    def test_candidate_rejects_selected_research_direction(self):
        document = candidate_document()
        document["output"]["research_direction"] = "flipped_positive"
        with self.assertRaisesRegex(ValueError, "raw_signed"):
            validate_document("candidate", document)

    def test_candidate_allows_family_specific_parameters(self):
        document = candidate_document()
        document["family_id"] = "flow_pressure"
        document["operator_id"] = "signed_trade_flow"
        document["parameters"] = {
            "half_life_events": 64,
            "normalization": "signed_total_volume",
            "include_cancels": False,
        }
        validate_document("candidate", document)

    def test_batch_rejects_duplicate_candidate_ids(self):
        document = batch_document()
        document["candidate_ids"] = ["candidate_a", "candidate_a"]
        with self.assertRaisesRegex(ValueError, "non-unique"):
            validate_document("batch", document)

    def test_l3_portrait_cannot_promote(self):
        document = portrait_document()
        document["decision"] = {
            "status": "promoted",
            "promotion_allowed": True,
            "reasons": ["short_sample_looked_good"],
        }
        with self.assertRaises(ValueError):
            validate_document("factor_portrait", document)

    def test_experience_requires_falsifiable_test(self):
        document = experience_document()
        del document["action"]["falsifiable_test"]
        with self.assertRaisesRegex(ValueError, "falsifiable_test"):
            validate_document("experience_record", document)

    def test_rejects_unknown_document_kind(self):
        with self.assertRaisesRegex(ValueError, "unknown contract kind"):
            validate_document("unknown", {})


if __name__ == "__main__":
    unittest.main()
