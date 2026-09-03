#include "factors/liquidity_resilience_v2/factor_entry.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace factors { namespace liquidity_resilience_v2 {
namespace {
const double kShockRatio = kShockThreshold;
const double kNaN = std::numeric_limits<double>::quiet_NaN();
double Liquidity(const Stock_Internal_Book& q, size_t levels) {
    const double bid=q.bp_array[0], ask=q.ap_array[0];
    if (!(bid>0.0 && ask>bid)) return 0.0;
    double volume=0.0; for (size_t i=0;i<levels;++i) {
        volume += static_cast<double>(q.bv_array[i]);
        volume += static_cast<double>(q.av_array[i]);
    }
    const double mid=0.5*(bid+ask), spread=ask-bid;
    const double value=volume/(1.0+spread/std::max(1.0,mid*0.001));
    return std::isfinite(value)&&value>0.0?value:0.0;
}
struct Bounds { size_t begin; size_t end; };
Bounds Window(size_t n,size_t w,size_t lag) {
    if (n<=lag) return {0,0};
    const size_t end=n-lag;
    return {end>w?end-w:0,end};
}
double Recovery(const std::deque<double>& h,size_t w,size_t lag) {
    const Bounds b=Window(h.size(),w,lag); if (b.end-b.begin<w) return kNaN;
    double peak=0.0; for(size_t i=b.begin;i<b.end;++i) peak=std::max(peak,h[i]);
    if (!(peak>0.0)) return kNaN;
    const double v=h[b.end-1]/peak; return std::isfinite(v)?std::max(0.0,std::min(1.0,v)):kNaN;
}
double Speed(const std::deque<double>& h,size_t w,size_t lag) {
    const Bounds b=Window(h.size(),w,lag); if (b.end-b.begin<w) return kNaN;
    // A shock must be a drawdown from a peak that was observed earlier.  Do
    // not compare a low value with a peak that appears later in the window:
    // that would misclassify a rising-only series as shock recovery.
    double running_peak=h[b.begin], shock_peak=0.0, minimum=0.0;
    size_t min_i=b.end;
    for(size_t i=b.begin+1;i+1<b.end;++i) {
        if (running_peak>0.0 && h[i] < running_peak*kShockRatio &&
            (min_i==b.end || h[i]<minimum)) {
            shock_peak=running_peak; minimum=h[i]; min_i=i;
        }
        running_peak=std::max(running_peak,h[i]);
    }
    // A recovery speed is defined only after a material drawdown and a later rebound.
    if (min_i==b.end || !(shock_peak>0.0)) return kNaN;
    const double rebound=h[b.end-1]-minimum; if (!(rebound>0.0)) return kNaN;
    const double denominator=shock_peak-minimum; if (!(denominator>0.0)) return kNaN;
    const double age=static_cast<double>(b.end-1-min_i);
    const double v=(rebound/denominator)/age;
    return std::isfinite(v)?v:kNaN;
}
}

FactorEntry::FactorEntry(const std::string& asset,const comm::FactorMetadata& metadata,const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset,metadata,config) {}
std::vector<bool> FactorEntry::GetReadinessMask(int64_t timestamp) const {
    (void)timestamp;
    std::vector<bool> ready(fvals_.size(), false);
    for (size_t i = 0; i < fvals_.size(); ++i) ready[i] = std::isfinite(fvals_[i]);
    return ready;
}
void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& q) {
    const double l1=Liquidity(q,1); if (!(l1>0.0)) { current_valid_=false; return; }
    const double l5=Liquidity(q,5); current_valid_=true; l1_.push_back(l1); l5_.push_back(l5>0.0?l5:l1);
    if(l1_.size()>kMaxHistoryEvents) l1_.pop_front();
    if(l5_.size()>kMaxHistoryEvents) l5_.pop_front();
}
void FactorEntry::DoOnUpdateFactors(int64_t) {
    if (!current_valid_) { std::fill(fvals_.begin(),fvals_.end(),kNaN); return; }
    fvals_[0]=Recovery(l1_,16,0); fvals_[1]=Recovery(l1_,32,0); fvals_[2]=Recovery(l1_,64,1); fvals_[3]=Recovery(l1_,128,2);
    fvals_[4]=Recovery(l5_,16,0); fvals_[5]=Recovery(l5_,32,0); fvals_[6]=Recovery(l5_,64,1); fvals_[7]=Recovery(l5_,128,2);
    fvals_[8]=Speed(l1_,16,0); fvals_[9]=Speed(l1_,32,0); fvals_[10]=Speed(l1_,64,1); fvals_[11]=Speed(l1_,128,2);
}
} }
