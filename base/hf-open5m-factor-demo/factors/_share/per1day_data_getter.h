#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace factors {
namespace share {

struct Per1DayData {
    std::string ticker;
    double high;
    double low;
    double open;
    double close;
    double vwap;
    double vol;
    double amount;
    double ashare;

    void Print() const {
        std::cout << "Ticker: " << ticker << "\n"
                  << "High: " << high << "\n"
                  << "Low: " << low << "\n"
                  << "Open: " << open << "\n"
                  << "Close: " << close << "\n"
                  << "VWAP: " << vwap << "\n"
                  << "Volume: " << vol << "\n"
                  << "Amount: " << amount << "\n"
                  << "Ashare: " << ashare << "\n\n";
    }
};

// 普通数据加载器类
class Per1DayDataGetter {
   public:
    // 构造函数，从文件加载数据
    explicit Per1DayDataGetter(const std::string& filename);

    // 获取所有数据
    const std::unordered_map<std::string, Per1DayData>& GetData() const;

    // 获取特定股票代码的数据
    const Per1DayData& GetData(const std::string& ticker) const;

   private:
    // 从文件读取数据
    std::unordered_map<std::string, Per1DayData> ReadCSV(
        const std::string& filename);

    // 实例数据
    std::unordered_map<std::string, Per1DayData> data_;
    Per1DayData default_data_;
};

bool get_codes_from_file(const std::string &file_path, std::string dataset_name, std::vector<std::string> &codes);

typedef struct
{
    char code[9];
    double open;
    double high;
    double low;
    double close;
    double vol;
    double amount;
    double adj;
    double close_nr;
    double preclose_nr;
    double vwap;
    double share;
    double ashare;
    int st;
    char swhy[12];
    int ipodates;
} dayinfo;

dayinfo* mxer_get_dayinfo(std::string dir_mxwork, std::string date, int &codenum);

bool mxer_get_trading_date_range(std::string dir_mxwork, int begt, int endt, std::vector<int> &dates);


}  // namespace share
}  // namespace factors