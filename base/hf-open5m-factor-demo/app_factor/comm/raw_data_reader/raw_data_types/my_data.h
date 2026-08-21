#pragma once

#pragma pack(push)
#pragma pack(8)

#include <vector>
#include <cstring>
#include <cmath>

typedef struct {
    char s_info_windcode[16];
    char b_info_issuer[32];
    double b_info_issueprice;
    double b_info_par;
    double b_info_couponrate;
    char b_info_carrydate[16];
    char b_info_enddate[16];
    char b_info_maturitydate[16];
    double b_info_paymenttype;
    int64_t b_info_interestfrequency;
    char s_info_name[24];
    char s_info_exchmarket[8];
    double is_incbonds;
    double years_to_maturity;
    char securityid[16];
    double bidyield;
    double askyield;
    int64_t count;
    int64_t maturity_bucket;
    char label[16];
} my_basic_data;

#pragma pack(pop)