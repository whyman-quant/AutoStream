#include "comm/tools.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>

void tools_Getfilepath(const char* path, const char* filename, char* filepath) {
	strcpy(filepath, path);
	if (filepath[strlen(path) - 1] != '/')
		strcat(filepath, "/");
	strcat(filepath, filename);
}

bool tools_DeleteFile(const char* path) {
	DIR* dir;
	struct dirent* dirinfo;
	struct stat statbuf;
	char filepath[256] = { 0 };
	lstat(path, &statbuf);

	if (S_ISREG(statbuf.st_mode)) {
		remove(path);
	} else if (S_ISDIR(statbuf.st_mode)) {
		if ((dir = opendir(path)) == NULL)
			return 1;
		while ((dirinfo = readdir(dir)) != NULL) {
			tools_Getfilepath(path, dirinfo->d_name, filepath);
			if (strcmp(dirinfo->d_name, ".") == 0 || strcmp(dirinfo->d_name, "..") == 0)
				continue;
			tools_DeleteFile(filepath);
			rmdir(filepath);
		}
		closedir(dir);
	}
	return 0;
}

void tools_CreateDir(const char* path) { mkdir(path, 0777); }

void tools_CreateDirRecursive(const char* path) {
    if (path == nullptr || *path == '\0') {
        std::cerr << "Error: Path is empty or null." << std::endl;
        return;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            std::cout << "Directory already exists: " << path << std::endl;
            return;
        } else {
            std::cerr << "Error: Path exists but is not a directory: " << path << std::endl;
            return;
        }
    }

    std::string current;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/') {
            if (!current.empty()) {
                if (mkdir(current.c_str(), 0777) == -1 && errno != EEXIST) {
                    std::cerr << "mkdir failed: " << strerror(errno) << " for " << current << std::endl;
                    return;
                }
            }
        }
        current += *p;
    }

    // 创建最后一级
    if (!current.empty()) {
        if (mkdir(current.c_str(), 0777) == -1 && errno != EEXIST) {
            std::cerr << "mkdir failed: " << strerror(errno) << " for " << current << std::endl;
            return;
        }
    }

    std::cout << "Directory created successfully: " << path << std::endl;
}

bool tools_IsFileExist(const char* path) {
	struct stat buffer;
	bool exist = (stat(path, &buffer) == 0);
	return exist;
}

bool tools_IsPathExist(const char* path) {
	struct stat buffer;
	bool exist = (stat(path, &buffer) == 0);
	return exist;
}

std::string tools_GetTempFilePath(const std::string& final_path) {
	return final_path + ".temp";
}

bool tools_CommitTempFile(const std::string& final_path, const std::string& temp_path) {
	if (!tools_IsPathExist(temp_path.c_str())) {
		std::cerr << "[ERROR] tools_CommitTempFile temp file does not exist: " << temp_path << std::endl;
		return false;
	}

	if (rename(temp_path.c_str(), final_path.c_str()) != 0) {
		int save_err = errno;
		std::cerr << "[ERROR] tools_CommitTempFile rename failed: " << temp_path << " -> " << final_path
				  << ", errno: " << save_err << " (" << strerror(save_err) << ")" << std::endl;
		return false;
	}

	std::string parent_dir = ".";
	size_t last_slash_pos = final_path.find_last_of('/');
	if (last_slash_pos != std::string::npos) {
		parent_dir = (last_slash_pos == 0) ? "/" : final_path.substr(0, last_slash_pos);
	}

	int dir_fd = open(parent_dir.c_str(), O_RDONLY | O_DIRECTORY);
	if (dir_fd < 0) {
		int save_err = errno;
		std::cerr << "[ERROR] tools_CommitTempFile open parent directory failed for fsync: " << parent_dir
				  << ", errno: " << save_err << " (" << strerror(save_err) << ")" << std::endl;
		return false;
	}
	if (fsync(dir_fd) != 0) {
		int save_err = errno;
		std::cerr << "[ERROR] tools_CommitTempFile fsync parent directory failed: " << parent_dir
				  << ", errno: " << save_err << " (" << strerror(save_err) << ")" << std::endl;
		close(dir_fd);
		return false;
	}
	if (close(dir_fd) != 0) {
		int save_err = errno;
		std::cerr << "[ERROR] tools_CommitTempFile close parent directory fd failed: " << parent_dir
				  << ", errno: " << save_err << " (" << strerror(save_err) << ")" << std::endl;
		return false;
	}
	return true;
}

bool tools_IsFile(const char* path) {
	struct stat buffer;
	if (stat(path, &buffer) != 0) {
		return false;
	}
	return S_ISREG(buffer.st_mode);
}

void tools_CopyFile(const char* src, const char* dst) {
	std::ifstream source(src, std::ios::binary);
	std::ofstream dest(dst, std::ios::binary);
	dest << source.rdbuf();
	source.close();
	dest.close();
}

// 扫描文件夹下的所有文件，文件名放入 file_names 中
void tools_ScanDir(const char* dir, std::vector<std::string>& file_names) {
	DIR* dirp;
	struct dirent* dp;
	dirp = opendir(dir);
	while ((dp = readdir(dirp)) != NULL) {
		file_names.push_back(dp->d_name);
	}
	closedir(dirp);
}

void tools_SplitString(const std::string& str, const std::string& sp, std::vector<std::string>& res) {
	std::string::size_type pos1, pos2;
	pos2 = str.find(sp);
	pos1 = 0;
	while (std::string::npos != pos2) {
		res.push_back(str.substr(pos1, pos2 - pos1));
		pos1 = pos2 + sp.size();
		pos2 = str.find(sp, pos1);
	}
	if (pos1 != str.length())
		res.push_back(str.substr(pos1));
}