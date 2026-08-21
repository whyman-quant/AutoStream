#pragma once

#include "sdp_handler/quote_format_define.h"

#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "factors/demofw00/tools/market/ofi_imbalance.h"
#include "factors/demofw00/tools/base/window_basic_stats.h"
#include "factors/demofw00/tools/base/base_utils.h"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

/*
	OfiStatusAnalyzer
	- 作用：基于 OfiImbalance（ofi_zscore、price_vol_zscore、ofi_trend_slope、state）
		在逐 tick 快照下衍生“状态/不平衡/纯度”指标。
	- 使用：每个快照事件调用 update(ofi_value, price_volatility, quote)
		其中 ofi_value 可取 ofi_multi_.ofi_agg_ 等，price_volatility 建议传 5s/30tick 的波动度或方差。
	- 输出：
		1) 状态独热与驻留时间
		2) 转移统计、SHOCK 强度与恢复历时
		3) 状态条件下的 E[ret_5s] / E[absret_5s] / 斜率均值
		4) 纯度指标：purity_state_gate / purity_slope / purity_joint / decay_shock / purity_payoff /
				   absorption_rate / sign_persistence / noise_penalty / purity_final
*/

// OFI 状态-不平衡分析器
// 聚焦于：
// 1) 状态独热、驻留时间（事件数/毫秒）、最近状态切换距今
// 2) 状态转移标记与计数、SHOCK 强度（|z| run mean/peak）、SHOCK→CALM 恢复时长
// 3) 状态条件下表现：E[ret_5s|state]、E[absret_5s|state]、买卖量不平衡(此模式下置0)、OFI 斜率均值
// 4) 联合不平衡：joint_z、slope_filtered_z
class OfiStatusAnalyzer {
public:
	// 构造：各统计窗口按事件数计数
	explicit OfiStatusAnalyzer(int per_state_window_count = 200,
							  int shock_run_window_count = 200,
							  int recovery_stats_window_count = 100)
		: per_state_window_count_(per_state_window_count),
		  shock_run_window_count_(shock_run_window_count),
		  recovery_stats_window_count_(recovery_stats_window_count),
		  shock_abs_z_stats_(shock_run_window_count),
		  recovery_durations_ms_(recovery_stats_window_count),
		  recovery_durations_events_(recovery_stats_window_count) {
		init_state_arrays_();
	}

	void reset() {
		event_idx_ = 0;
		curr_state_ = OfiMarketState::CALM;
		prev_state_ = OfiMarketState::CALM;
		last_state_change_event_idx_ = 0;
		last_state_change_time_ms_ = 0;
		last_transition_from_ = OfiMarketState::CALM;
		last_transition_to_ = OfiMarketState::CALM;
		has_last_transition_ = false;
		// 清理统计（逐状态）
		for (int i = 0; i < kNumStates; ++i) {
			ret5s_stats_[i] = base::WindowBasicStats(per_state_window_count_);
			absret5s_stats_[i] = base::WindowBasicStats(per_state_window_count_);
			bsi_stats_[i] = base::WindowBasicStats(per_state_window_count_);
			slope_stats_[i] = base::WindowBasicStats(per_state_window_count_);
		}
		// SHOCK run 统计与转移
		shock_abs_z_stats_ = base::WindowBasicStats(shock_run_window_count_);
		shock_run_abs_z_peak_ = 0.0;
		shock_block_active_ = false;
		shock_block_start_ms_ = 0;
		shock_block_start_event_idx_ = 0;
		recovery_durations_ms_ = base::WindowBasicStats(recovery_stats_window_count_);
		recovery_durations_events_ = base::WindowBasicStats(recovery_stats_window_count_);
		for (auto &row : transition_counts_) row.fill(0);
		// 5s 中位价均值
		mid5s_ = base::WindowBasicStats(5000);
		last_mid5s_mean_ = 0.0;
		has_last_mid5s_mean_ = false;
		last_mid_px_ = 0.0;
		// 纯度派生重置
		absorption_flag_stats_ = base::WindowBasicStats(absorption_window_);
		sign_run_len_ = 0; prev_sign_ = 0;
		// 内部不平衡器复位
		imbalance_.reset();
	}

	// —— 更新入口 ——
	// 输入：ofi_value（如 ofi_agg）、price_volatility（如 5s/30tick 方差/波动）、快照 quote
	// 计算：更新不平衡统计、中位价 5s 均值收益/逐 tick 绝对收益、状态与转移、纯度派生统计
	void update(double ofi_value, double price_volatility, const Stock_Internal_Book &quote) {
		// 先更新内部不平衡器
		imbalance_.update(ofi_value, price_volatility);
		// 时间与中位价
		int t_ms = base::decode_time(quote.exch_time);
		double mid = 0.5 * (static_cast<double>(quote.ap_array[0]) + static_cast<double>(quote.bp_array[0]));

		// 5s 中位价均值收益
		double ret5s = 0.0;
		mid5s_.update(t_ms, mid);
		double mean_now = mid5s_.mean();
		if (has_last_mid5s_mean_ && last_mid5s_mean_ > 0.0 && mean_now > 0.0) {
			ret5s = (mean_now - last_mid5s_mean_) / last_mid5s_mean_;
		}
		last_mid5s_mean_ = mean_now;
		has_last_mid5s_mean_ = true;

		// 逐tick绝对收益（基于中位价）
		double absret = 0.0;
		if (last_mid_px_ > 0.0 && mid > 0.0) absret = std::abs((mid - last_mid_px_) / last_mid_px_);
		last_mid_px_ = mid;

		// 买卖量不平衡（无成交信息时置0）
		core_update(t_ms, ret5s, absret, 0.0, 0.0);
	}

	// —— 对外查询接口 ——
	// 状态独热（0/1）
	int state_onehot_calm() const { return curr_state_ == OfiMarketState::CALM ? 1 : 0; }
	int state_onehot_shock() const { return curr_state_ == OfiMarketState::SHOCK ? 1 : 0; }
	int state_onehot_accumulation() const { return curr_state_ == OfiMarketState::ACCUMULATION ? 1 : 0; }
	int state_onehot_distribution() const { return curr_state_ == OfiMarketState::DISTRIBUTION ? 1 : 0; }
	int state_onehot_recovery() const { return curr_state_ == OfiMarketState::RECOVERY ? 1 : 0; }

	// 状态门控纯度：I(state∈allowed) × I(dwell_events≥K)
	int purity_state_gate() const {
		const bool allowed = is_allowed_state_(curr_state_);
		const bool dwell_ok = state_dwell_events() >= dwell_min_events_;
		return (allowed && dwell_ok) ? 1 : 0;
	}

	// 斜率一致性纯度：max(0, clamp(slope_filtered_z, -z_cap, z_cap))
	double purity_slope() const {
		double x = slope_filtered_z_;
		if (z_cap_ > 0.0) {
			if (x > z_cap_) x = z_cap_;
			else if (x < -z_cap_) x = -z_cap_;
		}
		return x > 0.0 ? x : 0.0;
	}

	// 联合强度纯度：max(0, clamp(joint_z, -z_cap, z_cap))
	double purity_joint() const {
		double x = joint_z_;
		if (z_cap_ > 0.0) {
			if (x > z_cap_) x = z_cap_;
			else if (x < -z_cap_) x = -z_cap_;
		}
		return x > 0.0 ? x : 0.0;
	}

	// 冲击/恢复衰减 (0,1]：1 / (1 + a*shock_mean + b*shock_peak + c / recovery_speed_events)
	double decay_shock() const {
		const double shock_mean = shock_intensity_abs_z_mean();
		const double shock_peak = shock_intensity_abs_z_peak();
		const double reco_ev = recovery_speed_events_mean();
		const double denom = 1.0 + alpha_shock_ * shock_mean + beta_shock_ * shock_peak + gamma_recovery_ / (1e-8 + std::max(0.0, reco_ev));
		return denom > 0.0 ? (1.0 / denom) : 1.0;
	}

	// 条件表现一致性：sign(slope_filtered_z) * E[ret5s|state] / (eps + E[absret5s|state])
	double purity_payoff() const {
		const OfiMarketState s = curr_state_;
		const double sign_sf = (slope_filtered_z_ > 0.0 ? 1.0 : (slope_filtered_z_ < 0.0 ? -1.0 : 0.0));
		const double mret = cond_mean_ret5s(s);
		const double mabs = cond_mean_absret5s(s);
		return sign_sf * mret / (1e-8 + std::abs(mabs));
	}

	// 吸收率：最近窗口内，高|ofi_z|且|ret5s|小的比例
	double absorption_rate() const { return absorption_flag_stats_.mean(); }

	// 符号持续度：基于 slope_filtered_z 的 run-length 归一
	double sign_persistence() const {
		return std::min(1.0, (run_max_ > 0 ? (static_cast<double>(sign_run_len_) / static_cast<double>(run_max_)) : 0.0));
	}

	// 噪声惩罚：1 / (1 + lambda * SE(absret5s|state))，映射到 [0,1]
	double noise_penalty() const {
		const int si = state_index_(curr_state_);
		const double se = absret5s_stats_[si].se();
		const double denom = 1.0 + lambda_noise_ * std::max(0.0, se);
		const double v = denom > 0.0 ? (1.0 / denom) : 1.0;
		return std::max(0.0, std::min(1.0, v));
	}

	// 最终纯度组合：门控×(w1*slope + w2*joint + w3*payoff)×衰减×持续度×噪声惩罚
	double purity_final() const {
		const double gate = static_cast<double>(purity_state_gate());
		if (gate <= 0.0) return 0.0;
		const double core = w1_ * purity_slope() + w2_ * purity_joint() + w3_ * purity_payoff();
		const double mult = std::pow(decay_shock(), p_decay_) * std::pow(sign_persistence(), p_persist_) * std::pow(noise_penalty(), p_noise_);
		return core * mult;
	}

	// 当前状态驻留：事件数与毫秒
	int state_dwell_events() const { return std::max(0, event_idx_ - last_state_change_event_idx_); }
	int state_dwell_ms(int now_ms) const { return std::max(0, now_ms - last_state_change_time_ms_); }
	// 与上同义：距离上次切换
	int time_since_state_change_events() const { return state_dwell_events(); }
	int time_since_state_change_ms(int now_ms) const { return state_dwell_ms(now_ms); }

	// 最近一次是否发生特定转移（one-shot）
	bool last_transition_flag(OfiMarketState from, OfiMarketState to) const {
		return has_last_transition_ && last_transition_from_ == from && last_transition_to_ == to;
	}
	// 累积转移次数
	int transition_count(OfiMarketState from, OfiMarketState to) const {
		return transition_counts_[state_index_(from)][state_index_(to)];
	}

	// SHOCK 强度
	double shock_intensity_abs_z_mean() const { return shock_abs_z_stats_.mean(); }
	double shock_intensity_abs_z_peak() const { return shock_run_abs_z_peak_; }

	// 恢复速度（均值）
	double recovery_speed_ms_mean() const { return recovery_durations_ms_.mean(); }
	double recovery_speed_events_mean() const { return recovery_durations_events_.mean(); }

	// 状态条件均值
	double cond_mean_ret5s(OfiMarketState s) const { return ret5s_stats_[state_index_(s)].mean(); }
	double cond_mean_absret5s(OfiMarketState s) const { return absret5s_stats_[state_index_(s)].mean(); }
	double cond_mean_buy_sell_imbalance(OfiMarketState s) const { return bsi_stats_[state_index_(s)].mean(); }
	double cond_mean_ofi_slope(OfiMarketState s) const { return slope_stats_[state_index_(s)].mean(); }

	// 联合不平衡
	double joint_z() const { return joint_z_; }
	double slope_filtered_z() const { return slope_filtered_z_; }

	OfiMarketState current_state() const { return curr_state_; }
	OfiMarketState previous_state() const { return prev_state_; }

private:
	// —— 核心更新（由 update 调用）——
	void core_update(int event_time_ms,
				   double ret5s,
				   double absret5s,
				   double buy_volume,
				   double sell_volume) {
		// 事件编号与状态推进
		++event_idx_;
		prev_state_ = curr_state_;
		curr_state_ = imbalance_.state_;

		// 转移与驻留
		if (prev_state_ != curr_state_) {
			has_last_transition_ = true;
			last_transition_from_ = prev_state_;
			last_transition_to_ = curr_state_;
			transition_counts_[state_index_(prev_state_)][state_index_(curr_state_)]++;
			last_state_change_event_idx_ = event_idx_;
			last_state_change_time_ms_ = event_time_ms;
		} else {
			has_last_transition_ = false;
		}

		// SHOCK 运行期统计
		const double abs_z = std::abs(imbalance_.ofi_zscore);
		if (prev_state_ != OfiMarketState::SHOCK && curr_state_ == OfiMarketState::SHOCK) {
			shock_abs_z_stats_ = base::WindowBasicStats(shock_run_window_count_);
			shock_run_abs_z_peak_ = 0.0;
		}
		if (curr_state_ == OfiMarketState::SHOCK) {
			shock_abs_z_stats_.update(event_idx_, abs_z);
			shock_run_abs_z_peak_ = std::max(shock_run_abs_z_peak_, abs_z);
		}

		// 记录 SHOCK→CALM 恢复时长（允许经由 RECOVERY 等中间状态）
		if (!shock_block_active_ && prev_state_ != OfiMarketState::SHOCK && curr_state_ == OfiMarketState::SHOCK) {
			shock_block_active_ = true;
			shock_block_start_ms_ = event_time_ms;
			shock_block_start_event_idx_ = event_idx_;
		}
		if (shock_block_active_ && curr_state_ == OfiMarketState::CALM) {
			int dur_ms = std::max(0, event_time_ms - shock_block_start_ms_);
			int dur_ev = std::max(0, event_idx_ - shock_block_start_event_idx_);
			recovery_durations_ms_.update(event_idx_, static_cast<double>(dur_ms));
			recovery_durations_events_.update(event_idx_, static_cast<double>(dur_ev));
			shock_block_active_ = false;
		}

		// 按状态条件统计：E[ret_5s|state], E[absret_5s|state], 买卖不平衡(0), ofi 斜率
		const int si = state_index_(curr_state_);
		ret5s_stats_[si].update(event_idx_, ret5s);
		absret5s_stats_[si].update(event_idx_, absret5s);
		double denom = buy_volume + sell_volume;
		double bsi = (denom > 1e-12) ? ((buy_volume - sell_volume) / denom) : 0.0;
		bsi_stats_[si].update(event_idx_, bsi);
		slope_stats_[si].update(event_idx_, imbalance_.ofi_trend_slope);

		// 联合不平衡与斜率门控 z
		joint_z_ = imbalance_.ofi_zscore * imbalance_.price_vol_zscore;
		double slope_gate = (imbalance_.ofi_trend_slope > 0.0 ? 1.0 : (imbalance_.ofi_trend_slope < 0.0 ? -1.0 : 0.0));
		slope_filtered_z_ = imbalance_.ofi_zscore * slope_gate;

		// 符号持续度（基于 slope_filtered_z）
		int curr_sign = (slope_filtered_z_ > 0.0 ? +1 : (slope_filtered_z_ < 0.0 ? -1 : 0));
		if (curr_sign == 0) {
			sign_run_len_ = 0;
		} else {
			if (curr_sign == prev_sign_) sign_run_len_ += 1; else sign_run_len_ = 1;
			prev_sign_ = curr_sign;
		}

		// 吸收率标记（高|z|且|ret5s|小）
		int absorb_flag = (std::abs(imbalance_.ofi_zscore) >= z_abs_th_ && std::abs(ret5s) <= ret_abs_th_) ? 1 : 0;
		absorption_flag_stats_.update(event_idx_, static_cast<double>(absorb_flag));
	}

	static constexpr int kNumStates = 5;

	static int state_index_(OfiMarketState s) {
		switch (s) {
			case OfiMarketState::CALM: return 0;
			case OfiMarketState::SHOCK: return 1;
			case OfiMarketState::ACCUMULATION: return 2;
			case OfiMarketState::DISTRIBUTION: return 3;
			case OfiMarketState::RECOVERY: return 4;
		}
		return 0;
	}

	bool is_allowed_state_(OfiMarketState s) const {
		if (s == OfiMarketState::ACCUMULATION) return allow_accumulation_;
		if (s == OfiMarketState::DISTRIBUTION) return allow_distribution_;
		if (s == OfiMarketState::CALM) return allow_calm_;
		if (s == OfiMarketState::SHOCK) return allow_shock_;
		if (s == OfiMarketState::RECOVERY) return allow_recovery_;
		return false;
	}

	void init_state_arrays_() {
		ret5s_stats_.reserve(kNumStates);
		absret5s_stats_.reserve(kNumStates);
		bsi_stats_.reserve(kNumStates);
		slope_stats_.reserve(kNumStates);
		for (int i = 0; i < kNumStates; ++i) {
			ret5s_stats_.emplace_back(per_state_window_count_);
			absret5s_stats_.emplace_back(per_state_window_count_);
			bsi_stats_.emplace_back(per_state_window_count_);
			slope_stats_.emplace_back(per_state_window_count_);
		}
		for (auto &row : transition_counts_) row.fill(0);
	}

	// —— 配置 ——
	int per_state_window_count_ = 200;
	int shock_run_window_count_ = 200;
	int recovery_stats_window_count_ = 100;
	// 纯度门控配置
	int dwell_min_events_ = 5;
	bool allow_calm_ = false;
	bool allow_shock_ = false;
	bool allow_accumulation_ = true;
	bool allow_distribution_ = true;
	bool allow_recovery_ = false;
	// 斜率纯度裁剪
	double z_cap_ = 6.0;
	// 冲击/恢复衰减参数
	double alpha_shock_ = 0.2;
	double beta_shock_ = 0.2;
	double gamma_recovery_ = 0.1;
	// 吸收率参数
	double z_abs_th_ = 1.5;
	double ret_abs_th_ = 1e-4;
	int absorption_window_ = 200;
	// 符号持续度参数
	int run_max_ = 20;
	// 噪声惩罚参数
	double lambda_noise_ = 1.0;
	// 组合权重与幂次
	double w1_ = 0.5, w2_ = 0.3, w3_ = 0.2;
	double p_decay_ = 1.0, p_persist_ = 1.0, p_noise_ = 1.0;

	// —— 状态 ——
	int event_idx_ = 0;
	OfiMarketState curr_state_ = OfiMarketState::CALM;
	OfiMarketState prev_state_ = OfiMarketState::CALM;
	int last_state_change_event_idx_ = 0;
	int last_state_change_time_ms_ = 0;

	bool has_last_transition_ = false;
	OfiMarketState last_transition_from_ = OfiMarketState::CALM;
	OfiMarketState last_transition_to_ = OfiMarketState::CALM;
	std::array<std::array<int, kNumStates>, kNumStates> transition_counts_{};

	// —— SHOCK 期 ——
	base::WindowBasicStats shock_abs_z_stats_;
	double shock_run_abs_z_peak_ = 0.0;
	bool shock_block_active_ = false;
	int shock_block_start_ms_ = 0;
	int shock_block_start_event_idx_ = 0;
	base::WindowBasicStats recovery_durations_ms_;
	base::WindowBasicStats recovery_durations_events_;

	// —— 状态条件统计 ——
	std::vector<base::WindowBasicStats> ret5s_stats_;
	std::vector<base::WindowBasicStats> absret5s_stats_;
	std::vector<base::WindowBasicStats> bsi_stats_;
	std::vector<base::WindowBasicStats> slope_stats_;

	// —— 联合不平衡 ——
	double joint_z_ = 0.0;
	double slope_filtered_z_ = 0.0;

	// —— 内部引擎与 5s 中位价均值 ——
	OfiImbalance imbalance_;
	base::WindowBasicStats mid5s_{5000};
	double last_mid5s_mean_ = 0.0;
	bool has_last_mid5s_mean_ = false;
	double last_mid_px_ = 0.0;

	// —— 纯度派生统计 ——
	base::WindowBasicStats absorption_flag_stats_{absorption_window_};
	int sign_run_len_ = 0;
	int prev_sign_ = 0;
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
