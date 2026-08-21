// tools：跨平台文件与字符串的小工具集合。失败多为静默/简单返回值，不抛异常。
#pragma once

#include <string>
#include <vector>

void tools_Getfilepath(const char* path, const char* filename, char* filepath);

bool tools_DeleteFile(const char* path);

// 创建单层目录（已存在时无副作用）
void tools_CreateDir(const char* path);

// 递归创建一个目录，权限设为0777
void tools_CreateDirRecursive(const char* path);

bool tools_IsFileExist(const char* path);

bool tools_IsPathExist(const char* path);

bool tools_IsFile(const char* path);

// 返回临时文件路径（final_path + ".temp"）
std::string tools_GetTempFilePath(const std::string& final_path);

// 将 temp 文件提交为最终文件，并对父目录做 fsync，尽量保证目录项落盘。
bool tools_CommitTempFile(const std::string& final_path, const std::string& temp_path);

void tools_CopyFile(const char* src, const char* dst);

// 扫描文件夹下的所有文件，文件名放入 file_names 中
void tools_ScanDir(const char* dir, std::vector<std::string>& file_names);

// 字符串分割：按分隔符 sp 拆分，忽略空片段的处理由调用者决定
void tools_SplitString(const std::string& str, const std::string& sp, std::vector<std::string>& res);
