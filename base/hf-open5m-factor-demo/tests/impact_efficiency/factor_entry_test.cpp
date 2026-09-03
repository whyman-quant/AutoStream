#include "factors/impact_efficiency/factor_entry.h"
#include "factors/_comm/factor_entry_manager.h"
#include <cmath>
#include <vector>
Stock_Transaction_Internal_Book_New T(char side,int price,int volume,char type='0'){Stock_Transaction_Internal_Book_New t{};t.bsflag=side;t.trade_price=price;t.trade_volume=volume;t.trade_type=type;return t;}
double V(factors::impact_efficiency::FactorEntry& e){return e.UpdateFactors(100000000).at(0);}
std::vector<double> Vs(factors::impact_efficiency::FactorEntry& e){return e.UpdateFactors(100000000);}
int main(){
 const std::vector<std::string> names={"impact_efficiency_signed_price_impact_w16","impact_efficiency_signed_price_impact_w32","impact_efficiency_signed_price_impact_w64","impact_efficiency_signed_price_impact_w128","impact_efficiency_mid_price_response_w16","impact_efficiency_mid_price_response_w32","impact_efficiency_mid_price_response_w64","impact_efficiency_mid_price_response_w128","impact_efficiency_ofi_response_w16","impact_efficiency_ofi_response_w32","impact_efficiency_absorption_divergence_w16","impact_efficiency_absorption_divergence_w32"};
 if(factors::impact_efficiency::GetMetadata().factor_names!=names)return 1;
 factors::comm::FactorEntryConfig c; factors::impact_efficiency::FactorEntry buy("000001",factors::impact_efficiency::GetMetadata(),c); buy.AddTrans(T('B',10000,100)); buy.AddTrans(T('B',10100,100)); if(!(V(buy)>0.0))return 1; auto values=Vs(buy); if(values.size()!=12)return 1; for(double x:values)if(!std::isfinite(x))return 1;
 auto opening_mask=buy.GetReadinessMask(92700000); if(opening_mask.size()!=12)return 1; for(size_t i=0;i<10;++i)if(opening_mask[i])return 1; if(!opening_mask[10]||!opening_mask[11])return 1;
 factors::FactorSetColumnLayout layout; layout["impact_efficiency"] = factors::FactorSetLayout(0, 12);
 factors::FactorEntryRowWriter writer("000001", c, {"impact_efficiency"}, layout, 12);
 writer.AddTrans(T('B', 10000, 100)); writer.AddTrans(T('B', 10100, 100));
 std::vector<unsigned char> readiness(12, 0); writer.WriteAllReadinessInto(92700000, readiness.data(), readiness.size());
 for(size_t i=0;i<10;++i)if(readiness[i])return 1; if(!readiness[10]||!readiness[11])return 1;
 auto regular_mask=buy.GetReadinessMask(93000000); for(bool ready:regular_mask)if(!ready)return 1;
 factors::impact_efficiency::FactorEntry sell("000001",factors::impact_efficiency::GetMetadata(),c); sell.AddTrans(T('S',10100,100)); sell.AddTrans(T('S',10000,100)); if(!(V(sell)>0.0))return 1; factors::impact_efficiency::FactorEntry flat("000001",factors::impact_efficiency::GetMetadata(),c); flat.AddTrans(T('B',10000,100)); flat.AddTrans(T('S',10000,100)); if(std::abs(V(flat))>1e-12)return 1; factors::impact_efficiency::FactorEntry bad("000001",factors::impact_efficiency::GetMetadata(),c); bad.AddTrans(T('B',10000,100,'C')); bad.AddTrans(T('B',0,0)); if(!std::isfinite(V(bad))||V(bad)!=0.0)return 1; return 0;}
