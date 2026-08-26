#include "factors/impact_efficiency/factor_entry.h"
#include <algorithm>
#include <cmath>
namespace factors { namespace impact_efficiency {
FactorEntry::FactorEntry(const std::string& asset,const comm::FactorMetadata& metadata,const comm::FactorEntryConfig& config):comm::FactorEntryBase(asset,metadata,config){}
void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote){(void)quote;}
void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order){(void)order;}
void FactorEntry::DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade){
 if(trade.trade_type=='C'||trade.trade_price<=0||trade.trade_volume<=0)return; double signed_volume=0.0; if(trade.bsflag=='B')signed_volume=static_cast<double>(trade.trade_volume); else if(trade.bsflag=='S')signed_volume=-static_cast<double>(trade.trade_volume); else return;
 trades_.push_back({static_cast<double>(trade.trade_price),signed_volume}); if(trades_.size()>kMaxHistoryEvents)trades_.pop_front();
}
namespace {
struct Bounds { size_t begin; size_t end; };
Bounds WindowBounds(size_t size,size_t window,size_t lag){if(size<=lag)return{0,0};size_t end=size-lag;return{end>window?end-window:0,end};}
double SignedImpact(const std::deque<FactorEntry::TradeState>& history,size_t window,size_t lag){auto b=WindowBounds(history.size(),window,lag);if(b.end-b.begin<2)return 0.0;double net=0,absvol=0;for(size_t i=b.begin;i<b.end;++i){net+=history[i].signed_volume;absvol+=std::abs(history[i].signed_volume);}double first=history[b.begin].price,last=history[b.end-1].price,mid=std::max(1.0,0.5*(first+last));double v=(last-first)/mid*(net/std::max(absvol,1.0));return std::isfinite(v)?v:0.0;}
double MidResponse(const std::deque<FactorEntry::TradeState>& history,size_t window,size_t lag){auto b=WindowBounds(history.size(),window,lag);if(b.end-b.begin<2)return 0.0;double net=0,absvol=0;for(size_t i=b.begin;i<b.end;++i){net+=history[i].signed_volume;absvol+=std::abs(history[i].signed_volume);}double first=history[b.begin].price,last=history[b.end-1].price,mid=std::max(1.0,0.5*(first+last));double v=(last-first)/mid*(net/std::max(absvol*mid,1.0));return std::isfinite(v)?v:0.0;}
double OfiResponse(const std::deque<FactorEntry::TradeState>& history,size_t window,size_t lag){auto b=WindowBounds(history.size(),window,lag);if(b.end-b.begin<2)return 0.0;double net=0,absvol=0;for(size_t i=b.begin;i<b.end;++i){net+=history[i].signed_volume;absvol+=std::abs(history[i].signed_volume);}double imbalance=net/std::max(absvol,1.0);if(std::abs(imbalance)<=1e-12)return 0.0;double first=history[b.begin].price,last=history[b.end-1].price,mid=std::max(1.0,0.5*(first+last));double v=((last-first)/mid)/imbalance;return std::isfinite(v)?v:0.0;}
double Absorption(const std::deque<FactorEntry::TradeState>& history,size_t window,size_t lag){auto b=WindowBounds(history.size(),window,lag);if(b.end-b.begin<2)return 0.0;double net=0,absvol=0;for(size_t i=b.begin;i<b.end;++i){net+=history[i].signed_volume;absvol+=std::abs(history[i].signed_volume);}double first=history[b.begin].price,last=history[b.end-1].price,mid=std::max(1.0,0.5*(first+last));double response=std::abs(last-first)/mid;double v=(net/std::max(absvol,1.0))*(1.0-response);return std::isfinite(v)?v:0.0;}
}
void FactorEntry::DoOnUpdateFactors(int64_t timestamp){(void)timestamp;fvals_[0]=SignedImpact(trades_,16,0);fvals_[1]=SignedImpact(trades_,32,0);fvals_[2]=SignedImpact(trades_,64,1);fvals_[3]=SignedImpact(trades_,128,2);fvals_[4]=MidResponse(trades_,16,0);fvals_[5]=MidResponse(trades_,32,0);fvals_[6]=MidResponse(trades_,64,1);fvals_[7]=MidResponse(trades_,128,2);fvals_[8]=OfiResponse(trades_,16,0);fvals_[9]=OfiResponse(trades_,32,0);fvals_[10]=Absorption(trades_,16,0);fvals_[11]=Absorption(trades_,32,0);}
} }
