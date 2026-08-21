#pragma once

#include "sdp_handler/quote_format_define.h"

#include "factors/demofw00/tools/base/online_ols.h"
#include "factors/demofw00/tools/base/online_pca.h"
#include "factors/demofw00/tools/base/window_basic_stats.h"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

// 市场状态
enum class OfiMarketState {
	CALM = 0,
	SHOCK,
	ACCUMULATION,
	DISTRIBUTION,
	RECOVERY
};

// 多档 OFI 计算器
class OfiCalculator {
public:
	int kLevels = 10; // 使用前 N 档

	// 聚合结果（对外可读）
	double avg_depth_ = 0.0;
	double ofi_basic_ = 0.0; // L1
	double ofi_agg_ = 0.0;   // 标准化求和
	double ofi_pca_ = 0.0;   // 主成分投影
	double ofi_order_ = 0.0; // 委托流口径（L1/可成交/补量）
	double ofi_trade_ = 0.0; // 成交流口径（买+ 卖-）


	OfiCalculator() : pca_(kLevels, 1) {
		prev_bid_px_.assign(kLevels, 0);
		prev_bid_vol_.assign(kLevels, 0);
		prev_ask_px_.assign(kLevels, 0);
		prev_ask_vol_.assign(kLevels, 0);
		raw_ofi_.assign(kLevels, 0.0);
		norm_ofi_.assign(kLevels, 0.0);
	}

	void reset() {
		initialized_ = false;
		std::fill(prev_bid_px_.begin(), prev_bid_px_.end(), 0);
		std::fill(prev_bid_vol_.begin(), prev_bid_vol_.end(), 0);
		std::fill(prev_ask_px_.begin(), prev_ask_px_.end(), 0);
		std::fill(prev_ask_vol_.begin(), prev_ask_vol_.end(), 0);
		std::fill(raw_ofi_.begin(), raw_ofi_.end(), 0.0);
		std::fill(norm_ofi_.begin(), norm_ofi_.end(), 0.0);
		ofi_basic_ = ofi_agg_ = ofi_pca_ = 0.0;
		ofi_order_ = ofi_trade_ = 0.0;
		avg_depth_ = 0.0;
		pca_.reset();
	}

	// 快照更新
	void update(const Stock_Internal_Book& book) {
		const int L = std::min(kLevels, 10);
		if (!initialized_) {
			for (int m = 0; m < L; ++m) {
				prev_bid_px_[m]  = book.bp_array[m];
				prev_bid_vol_[m] = book.bv_array[m];
				prev_ask_px_[m]  = book.ap_array[m];
				prev_ask_vol_[m] = book.av_array[m];
			}
			initialized_ = true;
			return; // 首次不产出有效 OFI
		}

		// 逐档 OFI
		for (int m = 0; m < L; ++m) {
			double ofi_m = 0.0;
			// 买侧贡献
			if (book.bp_array[m] > prev_bid_px_[m]) ofi_m += static_cast<double>(book.bv_array[m]);
			else if (book.bp_array[m] < prev_bid_px_[m]) ofi_m -= static_cast<double>(prev_bid_vol_[m]);
			else ofi_m += static_cast<double>(book.bv_array[m] - prev_bid_vol_[m]);
			// 卖侧贡献
			if (book.ap_array[m] < prev_ask_px_[m]) ofi_m -= static_cast<double>(book.av_array[m]);
			else if (book.ap_array[m] > prev_ask_px_[m]) ofi_m += static_cast<double>(prev_ask_vol_[m]);
			else ofi_m -= static_cast<double>(book.av_array[m] - prev_ask_vol_[m]);
			raw_ofi_[m] = ofi_m;
		}

		// 平均深度与聚合
		double sum_depth = 0.0;
		for (int m = 0; m < L; ++m) sum_depth += static_cast<double>(book.bv_array[m] + book.av_array[m]);
		avg_depth_ = (L > 0 && sum_depth > 1e-9) ? (sum_depth / (2.0 * L)) : 1.0;

		ofi_basic_ = raw_ofi_[0];
		ofi_agg_ = 0.0;
		for (int m = 0; m < L; ++m) {
			norm_ofi_[m] = raw_ofi_[m] / avg_depth_;
			ofi_agg_ += norm_ofi_[m];
		}

		// PCA（第一主成分投影）
		ofi_pca_ = 0.0;
		if (kLevels > 0) {
			std::vector<double> x(kLevels, 0.0);
			for (int i = 0; i < kLevels; ++i) x[i] = norm_ofi_[i];
			pca_.update(x);
			std::vector<double> v; double var1 = 0.0; pca_.principal_component(v, var1);
			double proj = 0.0; for (size_t i = 0; i < v.size() && i < x.size(); ++i) proj += v[i] * x[i];
			ofi_pca_ = proj;
		}

		// 更新 prev
		for (int m = 0; m < L; ++m) {
			prev_bid_px_[m]  = book.bp_array[m];
			prev_bid_vol_[m] = book.bv_array[m];
			prev_ask_px_[m]  = book.ap_array[m];
			prev_ask_vol_[m] = book.av_array[m];
		}
	}

	// 成交流
	void update(const Stock_Transaction_Internal_Book_New& trade) {
		if (trade.trade_volume <= 0 || trade.trade_type == 'C') return;
		if (trade.bsflag == 'B') ofi_trade_ += static_cast<double>(trade.trade_volume);
		else if (trade.bsflag == 'S') ofi_trade_ -= static_cast<double>(trade.trade_volume);
	}

	// 委托流（仅计 L1 撤单与 L1 补量，可成交单可按需计入）
	void update(const Stock_Order_Internal_Book_New& order) {
		if (order.order_volume <= 0) return;
		if (!initialized_) return; // 需有最近快照
		const int bid1 = prev_bid_px_.empty() ? 0 : static_cast<int>(prev_bid_px_[0]);
		const int ask1 = prev_ask_px_.empty() ? 0 : static_cast<int>(prev_ask_px_[0]);
		if (bid1 <= 0 || ask1 <= 0) return;

		const bool is_cancel = (order.order_type == 'C');
		const bool is_buy = (order.bsflag == 'B');
		const int p = order.order_price;
		const double q = static_cast<double>(order.order_volume);

		double delta = 0.0;
		if (is_cancel) {
			if (is_buy && p == bid1) delta = -q;               // 取消买一
			else if (!is_buy && p == ask1) delta = +q;         // 取消卖一
		} else {
			if (is_buy) {
				// 默认不计可成交，避免与成交双计；如需计入可放开下一行
				// if (p >= ask1) delta = +q;                     // 可成交买
				if (p == bid1) delta = +q;                      // 买一补量
			} else {
				// if (p <= bid1) delta = -q;                     // 可成交卖
				if (p == ask1) delta = -q;                      // 卖一补量
			}
		}
		ofi_order_ += delta;
	}

private:
	bool initialized_ = false;
	std::vector<int64_t> prev_bid_px_, prev_ask_px_;
	std::vector<int64_t> prev_bid_vol_, prev_ask_vol_;
	std::vector<double> raw_ofi_, norm_ofi_;
	base::OnlinePCA pca_;
};

// 标量 OFI 不平衡聚合与状态机（位置参数构造）
class OfiImbalance {
public:
	// 对外指标
	double ofi = 0.0;
	double ofi_mean = 0.0;
	double ofi_std = 0.0;
	double ofi_zscore = 0.0;
	double ofi_trend_slope = 0.0;
	double price_vol = 0.0;
	double price_vol_mean = 0.0;
	double price_vol_std = 0.0;
	double price_vol_zscore = 0.0;
	OfiMarketState state_ = OfiMarketState::CALM;
	int event_index_ = 0;

	// 位置参数构造（不使用 struct）
	OfiImbalance(int stats_capacity = 40,
				int trend_window_seconds = 120,
				double z_shock = 2.5,
				double z_normal = 1.0,
				double v_shock = 1.0,
				double v_normal = 0.5,
				double slope_pos_th = 0.0,
				double slope_neg_th = 0.0)
		: ofi_stats_(stats_capacity),
		  vol_stats_(stats_capacity),
		  ols_(trend_window_seconds),
		  z_shock_(z_shock), z_normal_(z_normal),
		  v_shock_(v_shock), v_normal_(v_normal),
		  slope_pos_th_(slope_pos_th), slope_neg_th_(slope_neg_th) {}

	void reset() {
		ofi_stats_ = base::WindowBasicStats(ofi_stats_window_());
		vol_stats_ = base::WindowBasicStats(vol_stats_window_());
		ofi_mean = ofi_std = ofi_zscore = 0.0;
		price_vol_mean = price_vol_std = price_vol_zscore = 0.0;
		event_index_ = 0;
		state_ = OfiMarketState::CALM;
	}

	void update(double ofi_value, double price_volatility) {
		// 滚动统计
        int idx = ++event_index_;
		ofi_stats_.update(idx, ofi_value);
		vol_stats_.update(idx, price_volatility);

		ofi_mean = ofi_stats_.mean();
		ofi_std  = ofi_stats_.std();
		price_vol_mean = vol_stats_.mean();
		price_vol_std  = vol_stats_.std();

		const double eps = 1e-12;
		ofi_zscore       = (std::abs(ofi_std)      > eps) ? ((ofi_value - ofi_mean) / ofi_std) : 0.0;
		price_vol_zscore = (std::abs(price_vol_std) > eps) ? ((price_volatility - price_vol_mean) / price_vol_std) : 0.0;
		ols_.update(idx, static_cast<double>(idx), ofi_value);
		ofi_trend_slope  = ols_.slope();

		apply_state_machine(ofi_zscore, price_vol_zscore, ofi_trend_slope);
	}

private:
	// 状态机：可同时参考 vol，也可仅看 ofi（将 v_* 设为极大/极小可退化）
	void apply_state_machine(double ofi_z, double vol_z, double trend_slope) {
		const double az = std::abs(ofi_z);
		const double vz = vol_z;
		auto shock_cond  = [&]() { return az >= z_shock_ && vz >= v_shock_; };
		auto normal_cond = [&]() { return az <= z_normal_ && vz <= v_normal_; };
		switch (state_) {
			case OfiMarketState::CALM:
				if (shock_cond()) state_ = OfiMarketState::SHOCK;
				else if (ofi_z > 0.0 && trend_slope > slope_pos_th_) state_ = OfiMarketState::ACCUMULATION;
				else if (ofi_z < 0.0 && trend_slope < slope_neg_th_) state_ = OfiMarketState::DISTRIBUTION;
				else state_ = OfiMarketState::CALM;
				break;
			case OfiMarketState::SHOCK:
				if (normal_cond()) state_ = OfiMarketState::RECOVERY; else state_ = OfiMarketState::SHOCK;
				break;
			case OfiMarketState::ACCUMULATION:
				if (shock_cond()) state_ = OfiMarketState::SHOCK;
				else if (ofi_z <= 0.0 || trend_slope <= slope_pos_th_) state_ = OfiMarketState::CALM;
				break;
			case OfiMarketState::DISTRIBUTION:
				if (shock_cond()) state_ = OfiMarketState::SHOCK;
				else if (ofi_z >= 0.0 || trend_slope >= slope_neg_th_) state_ = OfiMarketState::CALM;
				break;
			case OfiMarketState::RECOVERY:
				if (shock_cond()) state_ = OfiMarketState::SHOCK;
				else if (normal_cond()) state_ = OfiMarketState::CALM;
				break;
		}
	}

	int ofi_stats_window_() const { return ofi_stats_.N; }
	int vol_stats_window_() const { return vol_stats_.N; }

	// 统计与趋势
	base::WindowBasicStats ofi_stats_{40};
	base::WindowBasicStats vol_stats_{40};
	base::WindowOnlineOLS ols_{120};

	// 阈值参数
	double z_shock_ = 2.5, z_normal_ = 1.0;
	double v_shock_ = 1.0, v_normal_ = 0.5;
	double slope_pos_th_ = 0.0, slope_neg_th_ = 0.0;
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
