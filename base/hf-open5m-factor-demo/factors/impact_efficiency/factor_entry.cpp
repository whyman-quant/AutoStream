#include "factors/impact_efficiency/factor_entry.h"
#include <algorithm>
#include <cmath>
namespace factors { namespace impact_efficiency {
FactorEntry::FactorEntry(const std::string& asset,const comm::FactorMetadata& metadata,const comm::FactorEntryConfig& config):comm::FactorEntryBase(asset,metadata,config){}
void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote){(void)quote;}
void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order){(void)order;}
void FactorEntry::DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade){
 if(trade.trade_type=='C'||trade.trade_price<=0||trade.trade_volume<=0)return; double signed_volume=0.0; if(trade.bsflag=='B')signed_volume=static_cast<double>(trade.trade_volume); else if(trade.bsflag=='S')signed_volume=-static_cast<double>(trade.trade_volume); else return;
 trades_.push_back({static_cast<double>(trade.trade_price),signed_volume}); net_signed_volume_+=signed_volume; absolute_volume_+=std::abs(signed_volume); if(trades_.size()>kWindowEvents){auto old=trades_.front(); trades_.pop_front(); net_signed_volume_-=old.signed_volume; absolute_volume_-=std::abs(old.signed_volume);}
 if(trades_.size()<2){current_valid_=false;current_value_=0.0;return;} auto first=trades_.front(); auto last=trades_.back(); const double mid=std::max(1.0,0.5*(first.price+last.price)); const double value=(last.price-first.price)/mid*(net_signed_volume_/std::max(absolute_volume_,1.0)); current_value_=std::isfinite(value)?value:0.0; current_valid_=true;
}
void FactorEntry::DoOnUpdateFactors(int64_t timestamp){(void)timestamp; fvals_[0]=(current_valid_&&std::isfinite(current_value_))?current_value_:0.0;}
} }
