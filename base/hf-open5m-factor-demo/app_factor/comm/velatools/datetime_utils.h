#pragma once

#include <sys/time.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace velatools {
namespace datetime_utils {

// ============================================================================
// 系统时间获取和格式化相关函数
// ============================================================================
// 获取系统当前时间、时间戳转换和字符串格式化
// 适用于日志记录、时间显示等业务场景

// 获取当前时间戳（单位：微秒）
// 返回值：时间戳（微秒），例如1553736682830000 表示 2019-03-28 09:31:22.830000 (Asia/Shanghai)
inline long NowTimestampUs() {
	struct timeval tval;
	gettimeofday(&tval, nullptr);
	return tval.tv_sec * 1000000 + tval.tv_usec;
}

// 获取当前时间字符串（格式HH:MM:SS）
// 用法: std::string t = NowTimeStr(); std::cout << t << std::endl;
inline std::string NowTimeStr() {
	std::time_t t = std::time(nullptr);
	std::tm tm = *std::localtime(&t);
	char buf[20];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
	return std::string(buf);
}

// 获取当前时间字符串（格式HH:MM:SS.micros）
// 用法: std::string t = NowTimeUsStr(); std::cout << t << std::endl;
inline std::string NowTimeUsStr() {
	struct timeval tval;
	gettimeofday(&tval, nullptr);

	std::time_t seconds = static_cast<std::time_t>(tval.tv_sec);
	int micros = static_cast<int>(tval.tv_usec);
	std::tm local_time = *std::localtime(&seconds);

	char buf[40];
	std::snprintf(
		buf, sizeof(buf), "%02d:%02d:%02d.%06d",
		local_time.tm_hour, local_time.tm_min, local_time.tm_sec, micros
	);
	return std::string(buf);
}

// 将时间戳（微秒）转换为字符串（格式HH:MM:SS.micros）
// 参数说明：
//   timestamp_us - 时间戳（微秒），例如1553736682830000 表示 2019-03-28 09:31:22.830000 (Asia/Shanghai)
//   timezone     - 时区，例如8表示东八区，默认值为8
// 返回值：时间戳字符串，例如 "09:31:22.830000"
inline std::string GetTimeUsStrFromTimestampUs(long timestamp_us, int timezone = 8) {
	long timestamp_s = timestamp_us / 1000000;
	timestamp_s += static_cast<long>(timezone * 3600);

	int seconds_intraday = static_cast<int>(timestamp_s % 86400);
	int hh = seconds_intraday / 3600;
	int mm = (seconds_intraday % 3600) / 60;
	int ss = seconds_intraday % 60;
	int us = timestamp_us % 1000000;
	std::stringstream str_stream;
	str_stream << std::setw(2) << std::setfill('0') << hh << ":"
		<< std::setw(2) << std::setfill('0') << mm << ":"
		<< std::setw(2) << std::setfill('0') << ss << "."
		<< std::setw(6) << std::setfill('0') << us;
	return str_stream.str();
}

// ============================================================================
// HHMMSSms 格式相关常量和函数
// ============================================================================
// 交易日时间格式：HHMMSSms（例如 93027000 表示 09:30:27.000）
// 适用于交易时间计算、时间戳转换等交易系统场景

// HHMMSSms 格式常量
// 格式：HHMMSSms，例如 93027000 表示 09:30:27.000
static constexpr int kHH = 10000000;
static constexpr int kMM = 100000;
static constexpr int kSS = 1000;
static constexpr int kMS = 1;

inline void UnpackHHMMSSmsTimestamp(int stamp, int& h, int& m, int& s, int& ms) {
	h  = stamp / kHH;
	m  = (stamp % kHH) / kMM;
	s  = (stamp % kMM) / kSS;
	ms = stamp % kSS;
}

inline int AddMillisecondsToHHMMSSmsTimestamp(int stamp, int delta_ms) {
	int h, m, s, ms;
	UnpackHHMMSSmsTimestamp(stamp, h, m, s, ms);
	ms += delta_ms;
	s += ms / 1000; ms %= 1000;
	m += s / 60; s %= 60;
	h += m / 60; m %= 60;
	// 数据保证不跨天，h 不需要再规整
	return h * kHH + m * kMM + s * kSS + ms;
}

inline int AddSecondsToHHMMSSmsTimestamp(int stamp, int delta_sec) {
	int h, m, s, ms;
	UnpackHHMMSSmsTimestamp(stamp, h, m, s, ms);

	s += delta_sec;                  // 先加秒
	m += s / 60;  s %= 60;          // 秒→分
	h += m / 60;  m %= 60;          // 分→时
	// 数据保证不跨天，h 不需要再规整

	return h * kHH + m * kMM + s * kSS + ms;
}

// 将时间戳规范化为固定宽度文件名 stem。
// - 输入支持 HHMMSS（秒级）或 HHMMSSmmm（毫秒级）
// - trim_milliseconds=true  返回 6 位 HHMMSS（如 "093005"）
// - trim_milliseconds=false 返回 9 位 HHMMSSmmm（如 "093005123"）
inline std::string GetHHMMSSStemFromTimestamp(
	int timestamp_hhmmss_or_hhmmssmmm, bool trim_milliseconds = true) {
	if (timestamp_hhmmss_or_hhmmssmmm < 0) {
		return trim_milliseconds ? "000000" : "000000000";
	}

	const bool is_second_level = (timestamp_hhmmss_or_hhmmssmmm <= 240000);
	const int hhmmss = is_second_level ? timestamp_hhmmss_or_hhmmssmmm
									   : (timestamp_hhmmss_or_hhmmssmmm / 1000);
	const int ms = is_second_level ? 0 : (timestamp_hhmmss_or_hhmmssmmm % 1000);

	const int width = trim_milliseconds ? 6 : 9;
	std::string stem = trim_milliseconds ? std::to_string(hhmmss)
										 : (std::to_string(hhmmss) + std::to_string(ms));
	if (static_cast<int>(stem.size()) < width) {
		stem = std::string(width - static_cast<int>(stem.size()), '0') + stem;
	}
	return stem;
}

// 生成均匀间隔的 HHMMSSms 时间戳列表
// 参数说明：
//   start    - 起始时间戳（单位：毫秒，格式：HHMMSSms）
//   end      - 结束时间戳（单位：毫秒，格式：HHMMSSms）
//   interval - 时间间隔（单位：毫秒）
//   add      - 需要额外插入的时间戳（会在最后插入并去重排序）
//   skip     - 需要跳过的时间戳（这些时间戳不会出现在结果中）
// 返回值：
//   返回一个升序、去重的时间戳列表（单位：毫秒，格式：HHMMSSms）
inline std::vector<int> GenerateUniformHHMMSSmsTimestampList(
	int start, int end, int interval,
	const std::vector<int>& add, const std::vector<int>& skip) {
	// 将 skip 列表转为 unordered_set，便于快速查找
	std::unordered_set<int> skip_set(skip.begin(), skip.end());

	// 预分配空间，提升性能
	std::vector<int> times;
	times.reserve((end - start) / interval + 1);

	int time_hhmmssms = start;
	// 按 interval 步进生成时间戳
	while (time_hhmmssms <= end) {
		// 如果当前时间戳不在 skip_set 中，则加入结果
		if (skip_set.find(time_hhmmssms) == skip_set.end()) {
			times.push_back(time_hhmmssms);
		}
		// 增加 interval 毫秒
		time_hhmmssms = AddMillisecondsToHHMMSSmsTimestamp(time_hhmmssms, interval);
	}

	// 将 add 列表中的时间戳插入结果
	for (auto ts : add) {
		times.push_back(ts);
	}

	// 排序并去重
	std::sort(times.begin(), times.end());
	times.erase(std::unique(times.begin(), times.end()), times.end());

	return times;
}

inline int GetHHMMSSmsFromTimestampUs(long timestamp_us, int timezone = 8) {
	long timestamp_s = timestamp_us / 1000000;
	timestamp_s += static_cast<long>(timezone * 3600);

	int seconds_intraday = static_cast<int>(timestamp_s % 86400);
	int hh = seconds_intraday / 3600;
	int mm = (seconds_intraday % 3600) / 60;
	int ss = seconds_intraday % 60;
	int ms = (timestamp_us % 1000000) / 1000;
	return hh * 10000000 + mm * 100000 + ss * 1000 + ms;
}

struct HHMMSSmsWallClockTimer {
	int stamp;          // HHMMSSms  93027000 -> 09:30:27.000
private:
	bool unpacked_;
	int h_, m_, s_, ms_;

	void unpack()
	{
		if (unpacked_) return;
		h_  = stamp / kHH;
		m_  = (stamp % kHH) / kMM;
		s_  = (stamp % kMM) / kSS;
		ms_ = stamp % kSS;
		unpacked_ = true;
	}

public:
	explicit HHMMSSmsWallClockTimer(int s = 0) : stamp(s), unpacked_(false) {}

	// 接收新戳
	void Update(int new_stamp) {
		stamp = new_stamp;
		unpacked_ = false;
	}

	// 加若干秒（deltaSec 可正可负，调用方保证不跨天）
	// 数据不处理跨天的情况，调用方保证不跨天
	int AddSeconds(int delta_sec) {
		unpack();                      // 必要时拆包
		s_ += delta_sec;
		m_ += s_ / 60;  s_ %= 60;
		h_ += m_ / 60;  m_ %= 60;
		stamp = h_ * kHH + m_ * kMM + s_ * kSS + ms_;
		return stamp;
	}

	// 加若干毫秒（deltaMs 可正可负，调用方保证不跨天）
	// 数据不处理跨天的情况，调用方保证不跨天
	int AddMilliseconds(int delta_ms) {
		unpack();
		ms_ += delta_ms;
		s_ += ms_ / 1000; ms_ %= 1000;
		m_ += s_ / 60; s_ %= 60;
		h_ += m_ / 60; m_ %= 60;
		stamp = h_ * kHH + m_ * kMM + s_ * kSS + ms_;
		return stamp;
	}

	int Value() const { return stamp; }
};

} // namespace datetime_utils
} // namespace velatools
