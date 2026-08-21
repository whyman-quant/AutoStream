#pragma once

#include <string>

// 默认数据根（编译期常量，与 RootFromEnvironment / NormalizeRoot 行为一致；改默认只改此处）。
namespace data_path_defaults {
constexpr char kDefaultDataRootPath[] = "/data";
constexpr char kFutureBusinessRootPath[] = "/mnt/beegfs_npq_future";
constexpr char kDevBusinessRootPath[] = "/mnt/beegfs_npq107";
constexpr char kDefaultSsdRootPath[] = "/mnt/beegfs_npqssd";
constexpr char kFutureBusinessSsdRootPath[] = "/mnt/beegfs_futuressd";
} // namespace data_path_defaults

// .npq 相对路径模板（占位符 {date}/{symbol}/{fid}/{year}/{mi_type}）：拼在数据根之后解析。
// 与 origin/TicksData.cpp 里写的 "/data/..." 在语义上等价——origin 把默认根写进字符串；
// 此处只存根以下部分，绝对路径 = JoinBaseRelative(根, 替换占位符后的相对路径)，默认根见 data_path_defaults::kDefaultDataRootPath。
namespace npq_relative_template {
constexpr char kTick[] = "{date}/0/209/0/{symbol}.npq";
constexpr char kTransaction[] = "254/{year}/{date}/0/0/{symbol}.npq";
constexpr char kOrder[] = "253/{year}/{date}/0/0/{symbol}.npq";
constexpr char kOrderQueue[] = "252/{year}/{date}/0/0/{symbol}.npq";
constexpr char kOrderNew[] = "257/{year}/{date}/0/0/{symbol}.npq";
constexpr char kTransactionNew[] = "256/{year}/{date}/0/0/{symbol}.npq";
constexpr char kFutures[] = "{date}/0/{mi_type}/0/{symbol}.npq";
constexpr char kMc[] = "{date}/0/mc/0.npq";
constexpr char kEsmc[] = "{date}/0/esmc/0.npq";
} // namespace npq_relative_template

// C++ 侧数据根与路径拼接：与 Python NpqData.base_path、MYDATA_BUSINESS_TYPE 对齐；
// local_root_ 与 NpqData.local_path / 读盘时 local_or_mount 的「本地优先」对齐。
class DataPathConfig {
public:
	static std::string RootFromEnvironment();
	static std::string SsdRootFromEnvironment();
	static std::string NormalizeRoot(std::string base);
	static std::string JoinBaseRelative(const std::string &base, const std::string &relative);
	static bool IsDirectory(const std::string &path);
	static bool IsRegularFile(const std::string &path);

	DataPathConfig();
	explicit DataPathConfig(std::string root);

	const std::string &Root() const { return root_; }
	void SetRoot(std::string r) { root_ = NormalizeRoot(std::move(r)); }

	// 空字符串表示未设置（与 Python 未 set_local_path 时一致）。
	void SetLocalRoot(std::string l);
	void ClearLocalRoot() { local_root_.clear(); }
	const std::string &LocalRoot() const { return local_root_; }
	bool HasLocalRoot() const { return !local_root_.empty(); }

	// 读文件：若已设 local 且 local 下存在该相对路径的常规文件，则返回该绝对路径；否则返回 mount 根下路径。
	std::string ResolveReadableRelative(const std::string &relative) const;
	// 对齐 Python NpqData.path_parse：
	// 1) use_local_or_mount=true 且已设 local 时，用 local_root + relative；否则 root + relative。
	// 2) 然后尝试把结果中的 root 前缀映射到 NPQ_SSD_PATH（按环境），若映射路径存在则优先返回映射路径。
	std::string ResolvePathParseStyleRelative(const std::string &relative, bool use_local_or_mount) const;

	// QUOTE_NEW 判定：仅看 base(root_) 下是否存在名为 mi_type 的目录（对齐 Python _path_parse 行为）。
	bool MiTypeDirectoryExists(const std::string &mi_type_component) const;

private:
	std::string root_;
	std::string local_root_;
};

// 与 my.data.config.NpqData 中 QUOTE / COMPRESS_QUOTE 的 _path_parse 一致：
// 若 base(root) 下存在名为 mi_type 的目录（字符串目录名，如 "209"），则使用 QUOTE_NEW 模板；
// 否则使用旧式 "{date}/{day_night}/{mi_type}/{source}/{symbol}.(npq|myzst)"。
// day_night、source 与嵌入 Python 调用 my.data.quote.data 时常见取值 0 对齐。
// use_year_segment_after_mi：为 true 时 QUOTE_NEW 为「mi/year/date/dn/src/symbol」（254/253/252/256/257 等）；
// 为 false 时为「mi/date/dn/src/symbol」（209、期货 mi 等与旧 kTick/kFutures 新布局一致）。
void BuildPythonQuoteStyleRelativePaths(const DataPathConfig &cfg, int date, int mi_type, int day_night,
					int source, const std::string &symbol, bool use_year_segment_after_mi,
					std::string &rel_myzst_out, std::string &rel_npq_out);

// 231 QUOTE / QUOTE_NEW：与 TicksData::OrderTransRelativeArtifacts、Python _path_parse 一致（QUOTE_NEW 判定仅看 base(root)）。
inline void NpqOrderTransRelativeArtifacts(const DataPathConfig &cfg, int date, const std::string &symbol,
					   std::string &rel_myzst, std::string &rel_npq) {
	constexpr int kOrderTransMiType = 231;
	const std::string str_date = std::to_string(date);
	const std::string mi = std::to_string(kOrderTransMiType);
	const bool quote_new = cfg.MiTypeDirectoryExists(mi);
	std::string core;
	if (quote_new)
		core = mi + "/" + str_date + "/0/0/" + symbol;
	else
		core = str_date + "/0/" + mi + "/0/" + symbol;
	rel_myzst = core + ".myzst";
	rel_npq = core + ".npq";
}

namespace path_template {

inline void ReplaceAll(std::string *s, const char *pat, size_t pat_len, const std::string &val) {
	size_t pos = 0;
	while ((pos = s->find(pat, pos)) != std::string::npos) {
		s->replace(pos, pat_len, val);
		pos += val.size();
	}
}

// 将 relative 模板中的占位符全部替换；模板里未出现的占位符不产生效果。{year} 为 date 的 YYYY（to_string(date) 前 4 位）。
inline std::string FillPlaceholders(std::string rel, int date, const std::string &symbol_or_fid, int mi_type) {
	const std::string str_date = std::to_string(date);
	const std::string year = str_date.size() >= 4 ? str_date.substr(0, 4) : str_date;
	ReplaceAll(&rel, "{date}", 6, str_date);
	ReplaceAll(&rel, "{year}", 6, year);
	ReplaceAll(&rel, "{symbol}", 8, symbol_or_fid);
	ReplaceAll(&rel, "{fid}", 5, symbol_or_fid);
	ReplaceAll(&rel, "{mi_type}", 9, std::to_string(mi_type));
	return rel;
}

// data_type 须与 TicksData::DATA_TYPE 一致；当前覆盖 0/1/2/4/5/6/7/8/9。
inline const char *DefaultRelativeTemplate(int data_type) {
	switch (data_type) {
	case 0:
		return npq_relative_template::kTick;
	case 1:
		return npq_relative_template::kTransaction;
	case 2:
		return npq_relative_template::kOrder;
	case 4:
		return npq_relative_template::kOrderQueue;
	case 5:
		return npq_relative_template::kOrderNew;
	case 6:
		return npq_relative_template::kTransactionNew;
	case 7:
		return npq_relative_template::kMc;
	case 8:
		return npq_relative_template::kEsmc;
	case 9:
		return npq_relative_template::kFutures;
	default:
		return nullptr;
	}
}

} // namespace path_template

// 仅替换占位符，不拼数据根；扩展名写在模板字符串中即可（不限定 .npq）。
inline std::string FillPathTemplate(std::string relative_template, int date, const std::string &symbol_or_fid,
				    int mi_type = 0) {
	return path_template::FillPlaceholders(std::move(relative_template), date, symbol_or_fid, mi_type);
}

// 对一条相对模板做 FillPathTemplate 后，再经 cfg.ResolveReadableRelative 得到可读绝对路径。
inline std::string ResolvePathTemplate(const DataPathConfig &cfg, std::string relative_template, int date,
				       std::string symbol_or_fid, int mi_type) {
	return cfg.ResolveReadableRelative(
	    path_template::FillPlaceholders(std::move(relative_template), date, symbol_or_fid, mi_type));
}

// 按 TicksData::DATA_TYPE 从 npq_relative_template 取默认模板再解析：0–9 为占位符路径；10 为 ORDER_TRANS（走 NpqOrderTransRelativeArtifacts，无 {date} 模板）。
inline std::string ResolvePathTemplate(const DataPathConfig &cfg, int date, std::string symbol_or_fid, int data_type,
				       int mi_type) {
	if (data_type == 10) {
		std::string zst, npq;
		NpqOrderTransRelativeArtifacts(cfg, date, symbol_or_fid, zst, npq);
		return cfg.ResolveReadableRelative(npq);
	}
	const char *t = path_template::DefaultRelativeTemplate(data_type);
	if (!t || !*t)
		return cfg.ResolveReadableRelative(std::string());
	return ResolvePathTemplate(cfg, std::string(t), date, std::move(symbol_or_fid), mi_type);
}
