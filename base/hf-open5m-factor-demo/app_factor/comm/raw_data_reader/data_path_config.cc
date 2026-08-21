#include "data_path_config.h"

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace {

std::string rstrip_slash(std::string s) {
	while (!s.empty() && s.back() == '/')
		s.pop_back();
	return s;
}

std::string lstrip_slash(std::string s) {
	size_t i = 0;
	while (i < s.size() && s[i] == '/')
		++i;
	return s.substr(i);
}

bool has_prefix(const std::string &s, const std::string &prefix) {
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string replace_root_prefix(const std::string &path, const std::string &root, const std::string &ssd_root) {
	if (!has_prefix(path, root))
		return path;
	if (path.size() == root.size())
		return ssd_root;
	if (path[root.size()] == '/')
		return ssd_root + path.substr(root.size());
	return path;
}

} // namespace

std::string DataPathConfig::NormalizeRoot(std::string base) {
	if (base.empty())
		base = data_path_defaults::kDefaultDataRootPath;
	base = rstrip_slash(base);
	if (base.empty())
		base = "/";
	return base;
}

std::string DataPathConfig::RootFromEnvironment() {
	const char *biz = std::getenv("MYDATA_BUSINESS_TYPE");
	if (biz && std::strcmp(biz, "future") == 0)
		return NormalizeRoot(data_path_defaults::kFutureBusinessRootPath);
	if (biz && std::strcmp(biz, "dev") == 0)
		return NormalizeRoot(data_path_defaults::kDevBusinessRootPath);
	return NormalizeRoot(data_path_defaults::kDefaultDataRootPath);
}

std::string DataPathConfig::SsdRootFromEnvironment() {
	const char *biz = std::getenv("MYDATA_BUSINESS_TYPE");
	if (biz && std::strcmp(biz, "future") == 0)
		return NormalizeRoot(data_path_defaults::kFutureBusinessSsdRootPath);
	return NormalizeRoot(data_path_defaults::kDefaultSsdRootPath);
}

std::string DataPathConfig::JoinBaseRelative(const std::string &base, const std::string &relative) {
	std::string b = NormalizeRoot(base);
	std::string r = lstrip_slash(relative);
	if (r.empty())
		return b;
	if (b == "/")
		return std::string("/") + r;
	return b + "/" + r;
}

void DataPathConfig::SetLocalRoot(std::string l) {
	if (l.empty()) {
		local_root_.clear();
		return;
	}
	local_root_ = NormalizeRoot(std::move(l));
}

std::string DataPathConfig::ResolveReadableRelative(const std::string &relative) const {
	if (!HasLocalRoot()) {
		return JoinBaseRelative(root_, relative);
	}
	const std::string under_local = JoinBaseRelative(local_root_, relative);
	if (IsRegularFile(under_local))
		return under_local;
	return JoinBaseRelative(root_, relative);
}

std::string DataPathConfig::ResolvePathParseStyleRelative(const std::string &relative, bool use_local_or_mount) const {
	std::string selected_base = root_;
	if (use_local_or_mount && HasLocalRoot())
		selected_base = local_root_;

	const std::string path = JoinBaseRelative(selected_base, relative);
	const std::string ssd_root = SsdRootFromEnvironment();
	const std::string ssd_path = replace_root_prefix(path, root_, ssd_root);
	if (ssd_path != path && IsRegularFile(ssd_path))
		return ssd_path;
	return path;
}

bool DataPathConfig::MiTypeDirectoryExists(const std::string &mi_type_component) const {
	return IsDirectory(JoinBaseRelative(root_, mi_type_component));
}

void BuildPythonQuoteStyleRelativePaths(const DataPathConfig &cfg, int date, int mi_type, int day_night,
					int source, const std::string &symbol, bool use_year_segment_after_mi,
					std::string &rel_myzst_out, std::string &rel_npq_out) {
	const std::string str_date = std::to_string(date);
	const std::string mi = std::to_string(mi_type);
	const std::string dn = std::to_string(day_night);
	const std::string src = std::to_string(source);
	const std::string year = str_date.size() >= 4 ? str_date.substr(0, 4) : str_date;
	std::string core;
	if (cfg.MiTypeDirectoryExists(mi)) {
		if (use_year_segment_after_mi)
			core = mi + "/" + year + "/" + str_date + "/" + dn + "/" + src + "/" + symbol;
		else
			core = mi + "/" + str_date + "/" + dn + "/" + src + "/" + symbol;
	} else {
		core = str_date + "/" + dn + "/" + mi + "/" + src + "/" + symbol;
	}
	rel_myzst_out = core + ".myzst";
	rel_npq_out = core + ".npq";
}

DataPathConfig::DataPathConfig() : root_(RootFromEnvironment()) {}

DataPathConfig::DataPathConfig(std::string root) : root_(NormalizeRoot(std::move(root))) {}

bool DataPathConfig::IsDirectory(const std::string &path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
}

bool DataPathConfig::IsRegularFile(const std::string &path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	return S_ISREG(st.st_mode);
}
