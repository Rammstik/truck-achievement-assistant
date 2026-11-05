#include "../src/OfferFilter.h"
#include "../src/GameConfig.h"
#include <iostream>
#include <stdexcept>
using namespace aa::offers;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
json offer(std::string cargo,std::string city,std::string company,bool special=false){return {{"special_transport",special},{"cargo",{{"text_candidate",cargo}}},{"target",{{"city",{{"text_candidate",city}}},{"company",{{"text_candidate",company}}}}}};}
int main(){try{
 check(aa::game::IsATS&&std::string(aa::game::SdkId)=="ats","ATS compile selection");
 check(catalogue()["goals"].size()==140,"ATS catalogue missing");
 json model={{"account","a"},{"profile","p"},{"achievements",json::array()},{"details",{{"achievements",json::object()}}}};
 json prefs={{"account","a"},{"profile","p"},{"plan",json::object()}};
 auto select=[&](const std::vector<std::string>& ids){model["achievements"]=json::array();prefs["plan"]=json::object();for(const auto& id:ids){model["achievements"].push_back({{"id",id},{"name",id},{"status","progress"}});prefs["plan"]["a:p:"+id]=true;}SetSelection(model,prefs);return std::atomic_load(&current);};
 auto p=select({"az_phx_aport"});check(p->active,"airport filter inactive");
 check(allows(offer("frozen_veget","phoenix","aport_phx"),*p),"airport rejected");
 check(!allows(offer("frozen_veget","tucson","aport_phx"),*p),"wrong city accepted");
 check(!allows(offer("helicopter","phoenix","aport_phx",true),*p),"special transport leaked");
 model["details"]["achievements"]["az_phx_aport"]={{"kind","tracking"},{"items",json::array({{{"id","target:aport_phx.phoenix"},{"current",1},{"target",1}}})}};
 p=select({"az_phx_aport"});check(!allows(offer("a","phoenix","aport_phx"),*p),"confirmed depot retained");
 model["details"]["achievements"]["az_phx_aport"]["items"][0]["current"]=nullptr;
 p=select({"az_phx_aport"});check(allows(offer("a","phoenix","aport_phx"),*p),"unknown depot excluded");
 p=select({"az_phx_aport","wa_aerospace"});check(p->active&&p->goals.size()==2,"union missing");
 check(allows(offer("a","everett","dw_air_pln"),*p),"ATS company without cargo rejected");
 p=select({"az_phx_aport"});check(!allows(offer("a","everett","dw_air_pln"),*p),"removed achievement retained");
 p=select({"hc_allcargos"});check(allows(offer("coil","phoenix","a"),*p),"heavy cargo excluded");check(!allows(offer("frozen_veget","phoenix","a"),*p),"ordinary cargo passed heavy filter");
 model["details"]["achievements"]["hc_allcargos"]={{"kind","transported_cargos"},{"items",json::array({{{"id","coil"},{"visited",true}}})}};
 p=select({"hc_allcargos"});check(!allows(offer("coil","phoenix","a"),*p),"transported heavy cargo retained");
 auto sdCity=std::string();for(auto it=catalogue()["countries"].begin();it!=catalogue()["countries"].end();++it)if(it.value()=="south_dakota"){sdCity=it.key();break;}check(!sdCity.empty(),"ATS state mapping missing");
 model["details"]["achievements"]["sd_unique_cargos"]={{"kind","tracking"},{"items",json::array({{{"id","coil"},{"current",1},{"target",1}}})}};
 p=select({"sd_unique_cargos"});check(!allows(offer("coil",sdCity,"tay_con_sit2"),*p),"conditional unique cargo retained");check(allows(offer("other",sdCity,"tay_con_sit2"),*p),"new conditional unique cargo rejected");check(!allows(offer("other",sdCity,"wrong"),*p),"wrong unique depot accepted");
 auto cityGoal=std::string();for(auto it=catalogue()["goals"].begin();it!=catalogue()["goals"].end();++it)if(it.value()["class"]=="achievement_visit_city_data"){cityGoal=it.key();break;}
 p=select({cityGoal});check(!p->active,"city filtering default changed");prefs["filter_unvisited_cities"]=true;SetSelection(model,prefs);check(Status()["active"],"optional ATS city filter inactive");prefs["filter_unvisited_cities"]=false;
 unsigned supportedCount=0;
 for(auto it=catalogue()["goals"].begin();it!=catalogue()["goals"].end();++it){auto id=it.key();p=select({id});check(p->unsupported!="invalid_plan",id.c_str());auto geo=geography(*p);if(p->active){++supportedCount;allows(offer("coil","phoenix","a"),*p);}}
 p=select({});check(!p->active&&allows(offer("a","a","a",true),*p),"empty plan restricts offers");
 std::cout<<"ATS all 140 plans, union/removal, depot/cargo completion, unknowns, city option, Special Transport separation PASS; active delivery handlers="<<supportedCount<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
