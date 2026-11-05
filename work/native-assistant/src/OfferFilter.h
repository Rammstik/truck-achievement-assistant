#pragma once
#include "OfferRulesData.h"
#include "json.hpp"
#include <memory>
#include <atomic>
#include <mutex>
#include <set>
namespace aa::offers {
using json=nlohmann::json;
inline const json& catalogue(){static const json value=json::parse(RulesText());return value;}
struct Plan {bool active=false;uint64_t revision=0;json goals=json::array();std::set<std::string> confirmed;std::string unsupported,key;};
inline std::shared_ptr<const Plan> current=std::make_shared<Plan>();
inline std::mutex planMutex;
inline int compare(const std::string& a,const std::string& b){return a.empty()?0:a==b?1:-1;}
inline std::string cargoName(const json& offer){
    if(!offer.contains("cargo")||!offer["cargo"].is_object())return {};
    const auto& v=offer["cargo"];return v.contains("text_candidate")&&v["text_candidate"].is_string()?v["text_candidate"].get<std::string>():std::string();
}
inline std::string name(const json& offer,const char* endpoint,const char* field){
    if(!offer.contains(endpoint)||!offer[endpoint].is_object())return {};
    const auto& e=offer[endpoint];if(!e.contains(field)||!e[field].is_object())return {};
    const auto& v=e[field];return v.contains("text_candidate")&&v["text_candidate"].is_string()?v["text_candidate"].get<std::string>():std::string();
}
inline bool completedItem(const json& item){
    if(item.value("blocked",false))return false;
    if(item.value("confirmed",false))return true;
    if(!item.contains("current")||!item["current"].is_number())return false;
    const double target=item.value("target",1.0);
    return target>0&&item["current"].get<double>()>=target;
}
inline std::string country(const std::string& city){const auto& countries=catalogue()["countries"];return countries.contains(city)?countries[city].get<std::string>():std::string();}
inline int match(const std::string& uid,const json& offer,const Plan& plan,unsigned depth=0){
    const auto& units=catalogue()["units"];
    if(depth>40||!units.contains(uid))return 0;
    if(plan.confirmed.count(uid))return -1;
    const auto& u=units[uid];const auto& a=u["attrs"];auto type=u["class"].get<std::string>();const std::string prefix="achievement_delivery_";
    if(type=="achievement_unique_cargos"&&a.contains("condition")){
        auto cargo=cargoName(offer);if(!cargo.empty()&&plan.confirmed.count(cargo))return -1;return match(a["condition"],offer,plan,depth+1);
    }
    if(type=="achievement_unique_delivery_points"&&a.contains("condition")){
        const auto role=a.value("role",std::string("target"));const auto key=a.value("unique",std::string());
        auto value=name(offer,role.c_str(),"city");if(key=="country")value=country(value);
        if(!value.empty()&&plan.confirmed.count(role+":"+key+":"+value))return -1;
        return match(a["condition"],offer,plan,depth+1);
    }
    if(type.rfind(prefix,0)!=0)return 0;type.erase(0,prefix.size());
    if(type=="count")return a.contains("condition")?match(a["condition"],offer,plan,depth+1):0;
    if(type=="not"){int value=match(a["condition"],offer,plan,depth+1);return -value;}
    if(type=="each"||type=="any"||type=="sequence"){
        if(type=="sequence"&&a.value("in_order",false)){
            for(const auto& child:a["list"])if(!plan.confirmed.count(child.get<std::string>()))return match(child.get<std::string>(),offer,plan,depth+1);
            return -1;
        }
        const bool all=type=="each";bool unknown=false;
        for(const auto& child:a["list"]){int value=match(child.get<std::string>(),offer,plan,depth+1);if(all&&value<0)return -1;if(!all&&value>0)return 1;if(!value)unknown=true;}
        return unknown?0:all?1:-1;
    }
    if(type=="cargo"){
        std::string cargo;if(offer.contains("cargo")&&offer["cargo"].is_object()&&offer["cargo"].contains("text_candidate")&&offer["cargo"]["text_candidate"].is_string())cargo=offer["cargo"]["text_candidate"];
        int result=1;if(a.contains("cargo_name"))result=compare(cargo,a["cargo_name"]);
        if(result<0)return result;
        if(a.contains("adr")){
            const auto& cargos=catalogue()["cargos"];if(cargo.empty()||!cargos.contains(cargo)||!cargos[cargo].contains("adr"))return 0;
            auto adr=cargos[cargo]["adr"];if(!adr.is_number())return 0;
            bool ok=a["adr"]=="any_class"?adr.get<int>()>0:adr==a["adr"];if(!ok)return -1;
        }return result;
    }
    if(type=="company"||type=="point_city"||type=="point_country"||type=="city"||type=="country"){
        const auto role=offer.value("_role",a.value("role",std::string()));if(role!="source"&&role!="target")return 0;bool unknown=false,checked=false;
        if(a.contains("company_name")&&a.contains("city_name")&&plan.confirmed.count(role+":"+a["company_name"].get<std::string>()+"."+a["city_name"].get<std::string>()))return -1;
        for(const auto& spec:{std::pair<const char*,const char*>{"company_name","company"},{"city_name","city"},{"country_name","country"}}){
            if(!a.contains(spec.first))continue;checked=true;std::string value;
            if(std::string(spec.second)=="country"){auto city=name(offer,role.c_str(),"city");const auto& countries=catalogue()["countries"];if(countries.contains(city))value=countries[city];}
            else value=name(offer,role.c_str(),spec.second);
            int result=compare(value,a[spec.first]);if(result<0)return -1;if(!result)unknown=true;
        }return !checked||unknown?0:1;
    }
    // Damage, lateness, parking and other delivery outcomes cannot be known
    // before accepting a job. Such a job is a candidate, never a completion.
    return 0;
}
inline bool cargoList(const json& list,const std::string& cargo,const Plan& plan,bool excludeCompleted){
    for(const auto& value:list){auto id=value.get<std::string>();
        if(excludeCompleted&&plan.confirmed.count(id))continue;
        if(cargo.empty()||cargo==id)return true;
    }return false;
}
inline bool companyList(const json& list,const json& offer,const char* role,const Plan& plan){
    const auto company=name(offer,role,"company"),city=name(offer,role,"city");
    for(const auto& value:list){auto id=value.get<std::string>();
        if(plan.confirmed.count(id)||plan.confirmed.count(std::string(role)+":"+id))continue;
        const auto dot=id.find('.');if(dot==std::string::npos)continue;
        if((company.empty()||company==id.substr(0,dot))&&(city.empty()||city==id.substr(dot+1)))return true;
    }return false;
}
inline bool supported(const std::string& id,const json& unit){
    const auto type=unit["class"].get<std::string>();const auto& a=unit["attrs"];
    if(id=="st_all_routes")return false;
    if(type=="achievement_delivery")return a.contains("condition");
    if(type=="achievement_each_company_data")return a.contains("targets")||a.contains("sources");
    if(type=="achievement_each_cargo_data")return a.contains("cargos");
    if(type=="achievement_visit_city_data"||type=="achievement_each_delivery_point")return true;
    if(type=="achievement_limit_data")return id=="jobs_30_cargo"||id=="job_15_companies";
    if(type=="achievement_deliver_cargo_data")return a.contains("targets");
    if(type=="achievement_delivery_log_data"||type=="achievement_xp_data")return a.contains("cargos")||id=="ba_concrete"||id=="ba_to_russia"||id=="5_jobs_in_a_row"||id=="use_automatic_parking"||id=="no_autopark"||id=="harddifficlt"||id=="finish50jobs";
    return type=="achievement_cargo_group_data";
}
inline bool allows(const json& offer,const Plan& plan){
    if(!plan.active)return true;
    for(const auto& goal:plan.goals){
        const auto& a=goal["attrs"];Plan local;const auto type=goal["class"].get<std::string>();
        if(goal.contains("_confirmed"))for(const auto& id:goal["_confirmed"])local.confirmed.insert(id.get<std::string>());
        // Escorted offers have no ordinary company depots. They are candidates
        // only for Special Transport goals or the cross-market cargo groups.
        const bool special=offer.value("special_transport",false);
        const auto goalId=goal.value("_goal_id",std::string());
        const bool anyMarket=type=="achievement_cargo_group_data"||a.value("current",std::string())=="get_cargo_type_count"||goalId=="5_jobs_in_a_row"||goalId=="use_automatic_parking"||goalId=="no_autopark"||goalId=="harddifficlt"||goalId=="finish50jobs";
        if(!anyMarket&&special!=goal.value("_special",false))continue;
        const auto cargo=cargoName(offer);
        if(a.contains("cargos")&&!cargoList(a["cargos"],cargo,local,type=="achievement_each_cargo_data"))continue;
        if(a.contains("cargo")&&compare(cargo,a["cargo"])<0)continue;
        if(type=="achievement_visit_city_data"){
            const auto city=name(offer,"target","city");
            if(!city.empty()&&local.confirmed.count(city))continue;
            if(a.contains("cities")){bool found=city.empty();for(const auto& value:a["cities"])if(value==city)found=true;if(found)return true;}
            else if(a.contains("country_name")){if(compare(country(city),a["country_name"])>=0)return true;}
        }else if(type=="achievement_limit_data"){
            const auto field=a.value("current",std::string());const auto value=field=="get_cargo_type_count"?cargo:name(offer,"target","company");
            if(value.empty()||!local.confirmed.count(value))return true;
        }else if(type=="achievement_each_delivery_point"){
            const bool ordered=a.value("in_order",false);size_t index=0;
            for(const auto& source:a["sources"]){const auto i=index++;if(local.confirmed.count(std::to_string(i)))continue;
                auto from=offer,to=offer;from["_role"]="source";to["_role"]="target";
                bool allowed=match(source,from,local)>=0&&match(a["targets"][i],to,local)>=0;
                if(!allowed&&a.value("both_directions",false)){from["_role"]="target";to["_role"]="source";allowed=match(source,from,local)>=0&&match(a["targets"][i],to,local)>=0;}
                if(allowed)return true;if(ordered)break;
            }
        }else if(type=="achievement_delivery"){
            if(match(a["condition"],offer,local)>=0)return true;
        }else if(type=="achievement_cargo_group_data"){
            const auto& cargos=catalogue()["cargos"];
            if(cargo.empty()||!cargos.contains(cargo))return true;
            for(const auto& group:cargos[cargo]["groups"])if(!local.confirmed.count(group.get<std::string>()))return true;
        }else{
            bool match=true;
            for(const auto& spec:{std::pair<const char*,const char*>{"source_cities","source"},{"target_cities","target"}}){
                if(!a.contains(spec.first))continue;const auto city=name(offer,spec.second,"city");bool found=city.empty();for(const auto& value:a[spec.first])if(value==city)found=true;
                if(!found){match=false;break;}
            }
            for(const auto& spec:{std::pair<const char*,const char*>{"sources","source"},{"targets","target"},{"source_companies","source"},{"target_companies","target"}})
                if(a.contains(spec.first)&&!companyList(a[spec.first],offer,spec.second,local)){match=false;break;}
            if(match)return true;
        }
    }return false;
}
inline void SetSelection(const json& model,const json& preferences){
    auto next=std::make_shared<Plan>();
    try{
        const auto account=preferences.value("account",std::string()),profile=preferences.value("profile",std::string());
        if(model.value("account",std::string())==account&&model.value("profile",std::string())==profile&&preferences.contains("plan")){
            const auto& cat=catalogue();
            for(const auto& achievement:model.at("achievements")){
                auto id=achievement.value("id",std::string()),key=account+":"+profile+":"+id;
                if(achievement.value("status",std::string())=="done"||!preferences["plan"].contains(key)||preferences["plan"][key]!=true||!cat["goals"].contains(id))continue;
                const auto& definition=cat["goals"][id];auto type=definition["class"].get<std::string>();
                if(type=="achievement_visit_city_data"&&!preferences.value("filter_unvisited_cities",false))continue;
                const auto& unit=cat["units"][definition["unit"].get<std::string>()];
                if(supported(id,unit)){
                    json selectedUnit=unit;selectedUnit["_confirmed"]=json::array();selectedUnit["_special"]=id.rfind("st_",0)==0;selectedUnit["_goal_id"]=id;
                    if(model.contains("details")&&model["details"].contains("achievements")&&model["details"]["achievements"].contains(id)){
                        const auto& info=model["details"]["achievements"][id];
                        if(info.contains("items"))for(const auto& item:info["items"]){
                            // transported_cargos comes from the game's saved
                            // transported_cargo_types set, not estimated history.
                            const bool savedList=(info.value("kind",std::string())=="transported_cargos"||info.value("kind",std::string())=="visited_cities")&&item.value("visited",false);
                            // Positive evidence proves presence even if history is
                            // incomplete. Missing/null rows remain candidates.
                            const bool aggregateLog=type=="achievement_delivery_log_data"||type=="achievement_xp_data";
                            if(!aggregateLog&&(savedList||completedItem(item))){
                                const auto itemId=item.value("id",std::string());if(!itemId.empty())selectedUnit["_confirmed"].push_back(itemId);
                                if(type=="achievement_delivery"&&cat["units"].contains(unit["attrs"]["condition"].get<std::string>())){
                                    const auto& condition=cat["units"][unit["attrs"]["condition"].get<std::string>()];
                                    if(condition["class"]=="achievement_unique_delivery_points")selectedUnit["_confirmed"].push_back(condition["attrs"].value("role",std::string("target"))+":"+condition["attrs"].value("unique",std::string())+":"+itemId);
                                }
                            }
                            if(type=="achievement_each_delivery_point"&&info.value("kind",std::string())=="country_pairs"&&item.contains("countries")&&item.contains("deliveries")&&!item["deliveries"].empty()){
                                const auto& a=unit["attrs"];for(size_t n=0;n<a["sources"].size();++n){
                                    const auto first=cat["units"][a["sources"][n].get<std::string>()]["attrs"].value("country_name",std::string());
                                    const auto second=cat["units"][a["targets"][n].get<std::string>()]["attrs"].value("country_name",std::string());
                                    bool hasFirst=false,hasSecond=false;for(const auto& country:item["countries"]){hasFirst|=country==first;hasSecond|=country==second;}
                                    if(hasFirst&&hasSecond)selectedUnit["_confirmed"].push_back(std::to_string(n));
                                }
                            }
                        }
                    }
                    next->goals.push_back(std::move(selectedUnit));
                }else if(type.find("delivery")!=std::string::npos||type=="achievement_each_cargo_data"||type=="achievement_cargo_group_data"||type=="achievement_truck_brand_data"||type=="achievement_deliver_cargo_data")next->unsupported+=achievement.value("name",id)+"; ";
            }

        }
    }catch(...){next->goals=json::array();next->unsupported="invalid_plan";}
    next->active=!next->goals.empty()&&next->unsupported.empty();
    next->key=next->goals.dump()+next->unsupported;for(const auto& id:next->confirmed)next->key+=id;
    std::lock_guard lock(planMutex);auto previous=std::atomic_load(&current);if(previous->key==next->key)return;next->revision=previous->revision+1;std::atomic_store(&current,std::shared_ptr<const Plan>(next));
}
inline json Status(){auto plan=std::atomic_load(&current);return {{"active",plan->active},{"goals",plan->goals.size()},{"unsupported",plan->unsupported},{"revision",plan->revision}};}
struct Geography {std::string role,company,city,country;};
inline void collectGeography(const std::string& uid,const Plan& plan,std::vector<Geography>& out,const std::string& roleOverride={},unsigned depth=0){
    const auto& units=catalogue()["units"];if(depth>40||plan.confirmed.count(uid)||!units.contains(uid))return;
    const auto& unit=units[uid];const auto& a=unit["attrs"];const auto type=unit["class"].get<std::string>();
    if(type=="achievement_delivery_not")return;
    if(a.contains("condition")){collectGeography(a["condition"],plan,out,roleOverride,depth+1);return;}
    if(a.contains("list")){
        for(const auto& child:a["list"]){auto id=child.get<std::string>();if(plan.confirmed.count(id))continue;
            collectGeography(id,plan,out,roleOverride,depth+1);
            if(a.value("in_order",false))break;
        }return;
    }
    Geography geo{roleOverride.empty()?a.value("role",std::string()):roleOverride,a.value("company_name",std::string()),a.value("city_name",std::string()),a.value("country_name",std::string())};
    if(geo.role.empty()||(geo.company.empty()&&geo.city.empty()&&geo.country.empty()))return;
    if(!geo.company.empty()&&!geo.city.empty()&&plan.confirmed.count(geo.role+":"+geo.company+"."+geo.city))return;
    out.push_back(std::move(geo));
}
inline std::vector<Geography> geography(const Plan& selection){
    std::vector<Geography> out;
    // Unsupported delivery conditions disable order filtering, but must not
    // erase the independently known geography of other planned goals.
    for(const auto& goal:selection.goals){
        const auto type=goal["class"].get<std::string>();const auto& a=goal["attrs"];Plan local;
        for(const auto& id:goal["_confirmed"])local.confirmed.insert(id.get<std::string>());
        if(type=="achievement_visit_city_data"||type=="achievement_limit_data"||type=="achievement_cargo_group_data")continue;
        if(type=="achievement_delivery"){collectGeography(a["condition"],local,out);continue;}
        if(type=="achievement_each_delivery_point"){
            for(size_t i=0;i<a["sources"].size();++i){if(local.confirmed.count(std::to_string(i)))continue;
                collectGeography(a["sources"][i],local,out,"source");collectGeography(a["targets"][i],local,out,"target");if(a.value("in_order",false))break;
            }continue;
        }
        for(const auto& spec:{std::pair<const char*,const char*>{"sources","source"},{"targets","target"},{"source_companies","source"},{"target_companies","target"}}){
            if(!a.contains(spec.first))continue;
            for(const auto& value:a[spec.first]){auto id=value.get<std::string>();if(local.confirmed.count(id)||local.confirmed.count(std::string(spec.second)+":"+id))continue;auto dot=id.find('.');
                if(dot!=std::string::npos)out.push_back({spec.second,id.substr(0,dot),id.substr(dot+1),{}});
            }
        }
        for(const auto& spec:{std::pair<const char*,const char*>{"source_cities","source"},{"target_cities","target"}})
            if(a.contains(spec.first))for(const auto& city:a[spec.first])out.push_back({spec.second,{},city.get<std::string>(),{}});
    }return out;
}
}
