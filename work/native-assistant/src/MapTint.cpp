#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MapTint.h"
#include "CityMapTargets.h"
#include "TriggerMapTargets.h"
#include "FeatureMapTargets.h"
#include "OfferFilter.h"
#include "MapProbeTarget.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <set>
#include <unordered_map>
#include <chrono>
extern "C" unsigned __int32 AAMapTintBridge(float,float,float,float);
namespace aa::maptint {
namespace {
using json=nlohmann::json;
using PackFn=uint32_t(__fastcall*)(float,float,float,float);
PackFn original=nullptr;
void* hookTarget=nullptr;
uintptr_t expectedReturn=0;
std::atomic<bool> active{false};
std::atomic<unsigned> calls{0};
std::atomic<uint64_t> mapColors{0},changed{0},unreadable{0};
std::mutex mutex;
struct Point {double x,z;uint32_t kind=30;double y=0;unsigned category=0;};
std::vector<Point> points;
std::vector<Marker> markerCache;
std::unordered_map<uint64_t,std::vector<Point>> pointGrid;
uint64_t gridKey(int32_t x,int32_t z){return (uint64_t(uint32_t(x))<<32)|uint32_t(z);}
size_t logicalTargets=0;
std::string reason="disabled";
bool copy(uintptr_t address,void* output,size_t size) {
    if(!address||address>UINTPTR_MAX-size)return false;
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&got)&&got==size;
}
struct Inside {Inside(){++calls;}~Inside(){--calls;}};
uint32_t tint(uint32_t packed,float alpha,uintptr_t frame,uintptr_t returnAddress) {
    if(!active.load()||returnAddress!=expectedReturn||frame<0x80)return packed;
    ++mapColors;
    uintptr_t entry=0;std::array<unsigned char,40> raw{};
    if(!copy(frame-0x80,&entry,8)||!copy(entry,raw.data(),raw.size())){++unreadable;return packed;}
    uint32_t kind=0;float position[3]{};memcpy(&kind,raw.data()+20,4);memcpy(position,raw.data()+8,12);
    // Observed live: 30 = unviewed; 31 = viewed. Never tint a viewed icon.
    if((kind!=30&&kind!=10&&kind!=12)||!std::isfinite(position[0])||!std::isfinite(position[2])||std::abs(position[0])>10000000||std::abs(position[2])>10000000)return packed;
    bool match=false;
    {std::lock_guard lock(mutex);const auto x=int32_t(std::floor(position[0]*10)),z=int32_t(std::floor(position[2]*10));
     for(int dx=-1;dx<=1&&!match;++dx)for(int dz=-1;dz<=1&&!match;++dz){auto cell=pointGrid.find(gridKey(x+dx,z+dz));if(cell==pointGrid.end())continue;
        for(const auto& point:cell->second)if(point.kind==kind&&std::abs(point.x-position[0])<0.05&&std::abs(point.z-position[2])<0.05){match=true;break;}}}
    if(!match)return packed;
    ++changed;
    // Use the game's own packer, retaining its original alpha byte exactly.
    return (packed&0xff000000u)|(original(1.0f,0.0f,0.0f,alpha)&0x00ffffffu);
}
}
bool Install(void* imageBase) {
    Stop();points.clear();mapColors=changed=unreadable=0;
    const auto base=reinterpret_cast<uintptr_t>(imageBase);
    std::array<unsigned char,32> signature{};std::array<unsigned char,5> call{};
    if(!copy(base+mapprobe::target::PackRva,signature.data(),signature.size())||
       !copy(base+mapprobe::target::PackCallRva,call.data(),call.size())||
       memcmp(signature.data(),mapprobe::target::PackSignature,signature.size())||
       memcmp(call.data(),mapprobe::target::PackCallSignature,call.size())){reason="signature_mismatch";return false;}
    hookTarget=reinterpret_cast<void*>(base+mapprobe::target::PackRva);
    expectedReturn=base+mapprobe::target::PackReturnRva;
    if(MH_CreateHook(hookTarget,reinterpret_cast<void*>(&AAMapTintBridge),reinterpret_cast<void**>(&original))!=MH_OK){hookTarget=nullptr;reason="hook_create_failed";return false;}
    active=true;
    if(MH_EnableHook(hookTarget)!=MH_OK){Stop();reason="hook_enable_failed";return false;}
    reason="ready";return true;
}
void Stop() {
    active=false;
    if(hookTarget)MH_DisableHook(hookTarget);
    while(calls.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(hookTarget)MH_RemoveHook(hookTarget);
    hookTarget=nullptr;original=nullptr;
    std::lock_guard lock(mutex);points.clear();markerCache.clear();pointGrid.clear();logicalTargets=0;
}
void SetSelection(const json& model,const json& preferences) {
    aa::offers::SetSelection(model,preferences);
    std::vector<Point> selected;std::set<std::string> selectedIds;
    try {
        const auto account=preferences.value("account",std::string()),profile=preferences.value("profile",std::string());
        if(!account.empty()&&!profile.empty()&&model.value("account",std::string())==account&&model.value("profile",std::string())==profile&&
            model.contains("details")&&model["details"].contains("achievements")&&preferences.contains("plan")) {
            const auto& details=model["details"]["achievements"];const auto& plan=preferences["plan"];
            for(const auto& achievement:model.at("achievements")) {
                auto id=achievement.value("id",std::string());auto key=account+":"+profile+":"+id;
                if(achievement.value("status",std::string())=="done"||!plan.contains(key)||plan[key]!=true||!details.contains(id))continue;
                const auto& info=details[id];
                const auto context=model["details"].value("map_progress",json::object());
                for(const auto& feature:FeaturePositions){
                    bool wanted=false;
                    if(id=="use_train")wanted=feature.kind==3&&feature.train;
                    else if(id=="use_boat")wanted=feature.kind==3&&!feature.train;
                    else if(id=="use_all_ports"){
                        wanted=feature.kind==3;
                        if(wanted&&context.contains("used_ports")&&context["used_ports"].is_array())for(const auto& port:context["used_ports"])if(port==feature.port)wanted=false;
                    }else if(id=="use_gas_station"||id=="use_gas_stat")wanted=feature.kind==4;
                    else if(id=="use_weigh_stat"||id=="hc_weigh170k")wanted=feature.kind==8;
                    else if(id=="buy_truck")wanted=feature.kind==9;
                    else if(id=="custompj")wanted=feature.kind==6;
                    else if(id=="wa_use_ferry")wanted=feature.kind==3&&(std::string(feature.port)=="coupeville"||std::string(feature.port)=="porttownsend");
                    else if(id=="use_rest_stop")wanted=feature.kind==5;
                    else if(id=="metallic_multi_color_paint")wanted=feature.kind==6;
                    else if(id=="discover_all_agencies"){
                        wanted=feature.kind==7;
                        if(wanted&&*feature.city&&context.contains("recruitment_cities")&&context["recruitment_cities"].is_array())
                            for(const auto& city:context["recruitment_cities"])if(city==feature.city)wanted=false;
                    }else if(feature.kind==2){
                        int status=-1;if(context.contains("garages"))for(const auto& garage:context["garages"])if(garage.value("city",std::string())==feature.city&&garage.contains("status")&&garage["status"].is_number_integer())status=garage["status"];
                        if(id=="maxed_garage")wanted=status!=3;
                        else if(id=="quick_travel_to_hq")wanted=context.value("hq_city",json())==feature.city;
                        else if(id=="garage_in_every_city")wanted=status<=0;
                        else if(id=="it_garages")wanted=(std::string(feature.city)=="catania"||std::string(feature.city)=="messina"||std::string(feature.city)=="palermo")&&status<=0;
                        else if(id=="garages_in_hq_country")wanted=context.value("hq_country",json())==aa::offers::country(feature.city)&&status!=3;
                        else if(id=="large_garage_productivity"||id=="productivity_all_garages")wanted=status==3;
                    }
                    if(wanted){selected.push_back({feature.x,feature.z,12,feature.y,3});selectedIds.insert("feature:"+std::string(feature.uid));}
                }
                const auto kind=info.value("kind",std::string());
                const auto& catalogue=aa::offers::catalogue();
                if(catalogue["goals"].contains(id)){
                    const auto& unit=catalogue["units"][catalogue["goals"][id]["unit"].get<std::string>()];
                    if(!aa::offers::supported(id,unit))for(const auto* field:{"source_cities","target_cities"})if(unit["attrs"].contains(field))
                        for(const auto& city:unit["attrs"][field])for(const auto& location:CityPositions)if(city==location.city){selected.push_back({location.x,location.z,10});selectedIds.insert("city:"+std::string(location.city));}
                }
                if(kind=="special_routes"&&info.contains("items")){
                    for(const auto& item:info["items"]){if(item.value("logged",false))continue;
                        for(const auto* field:{"from_city","to_city"}){const auto city=item.value(field,std::string());
                            for(const auto& location:CityPositions)if(city==location.city){selected.push_back({location.x,location.z,10});selectedIds.insert("city:"+city);}}
                    }continue;
                }
                if(kind=="tracking") {
                    for(const auto& trigger:TriggerPositions)if(id==trigger.goal){
                        selected.push_back({trigger.x,trigger.z,12,trigger.y,1});selectedIds.insert("trigger:"+std::string(trigger.uid));
                    }
                    continue;
                }
                if((kind!="viewpoints"&&kind!="visited_cities")||!info.value("available",true)||info.value("needed",1)<=0)continue;
                for(const auto& item:info.at("items")) {
                    if(kind=="visited_cities") {
                        if(!item.contains("visited")||item["visited"]!=false)continue;
                        const auto city=item.value("id",std::string());
                        for(const auto& location:CityPositions)if(city==location.city){selected.push_back({location.x,location.z,10});selectedIds.insert("city:"+city);}
                    } else {
                        if(!item.contains("viewed")||item["viewed"]!=false||!item.contains("x")||!item.contains("z"))continue;
                        double x=item["x"].get<double>(),z=item["z"].get<double>();
                        if(std::isfinite(x)&&std::isfinite(z)){selected.push_back({x,z,30});selectedIds.insert("point:"+std::to_string(x)+":"+std::to_string(z));}
                    }
                }
            }
            const auto selectedPlan=std::atomic_load(&aa::offers::current);
            std::set<std::string> companyCities;
            for(const auto& target:aa::offers::geography(*selectedPlan)){
                if(!target.company.empty()){
                    for(const auto& feature:FeaturePositions)if(feature.kind==1&&target.company==feature.company&&
                       (target.city.empty()||target.city==feature.city)&&(target.country.empty()||target.country==aa::offers::country(feature.city))){
                        selected.push_back({feature.x,feature.z,12,feature.y,2});selectedIds.insert("feature:"+std::string(feature.uid));
                        if(*feature.city)companyCities.insert(feature.city);
                    }
                }else{
                    for(const auto& location:CityPositions)if((target.city.empty()||target.city==location.city)&&
                       (target.country.empty()||target.country==aa::offers::country(location.city))){
                        selected.push_back({location.x,location.z,10});selectedIds.insert("city:"+std::string(location.city));
                    }
                }
            }
            for(const auto& city:companyCities)if(!selectedIds.count("city:"+city))
                for(const auto& location:CityPositions)if(city==location.city){selected.push_back({location.x,location.z,10});selectedIds.insert("city:"+city);}
        }
    }catch(...){selected.clear();selectedIds.clear();}
    std::lock_guard lock(mutex);points=std::move(selected);logicalTargets=selectedIds.size();pointGrid.clear();markerCache.clear();
    std::set<std::pair<float,float>> markers;
    for(const auto& p:points){pointGrid[gridKey(int32_t(std::floor(p.x*10)),int32_t(std::floor(p.z*10)))].push_back(p);
        if(p.kind==12&&markers.insert({float(p.x),float(p.z)}).second)markerCache.push_back({float(p.x),float(p.y),float(p.z)});}
}
std::vector<Marker> Markers(){std::lock_guard lock(mutex);return active.load()?markerCache:std::vector<Marker>();}
json Status(){std::lock_guard lock(mutex);size_t cities=0,viewpoints=0,triggers=0,companies=0,services=0;for(const auto& p:points){if(p.kind==10)++cities;else if(p.category==1)++triggers;else if(p.category==2)++companies;else if(p.category==3)++services;else ++viewpoints;}return {{"active",active.load()},{"reason",reason},{"targets",logicalTargets},{"coordinate_positions",points.size()},{"trigger_positions",triggers},{"company_positions",companies},{"service_positions",services},{"marker_positions",markerCache.size()},{"city_positions",cities},{"viewpoint_positions",viewpoints},{"color","bright_red"},{"map_colors",mapColors.load()},{"tinted",changed.load()},{"unreadable",unreadable.load()}};}
}
extern "C" uint32_t AAMapTintPack(float r,float g,float b,float a,uintptr_t frame,uintptr_t caller) {
    aa::maptint::Inside inside;
    const auto packed=aa::maptint::original(r,g,b,a);
    try{return aa::maptint::tint(packed,a,frame,caller);}catch(...){return packed;}
}
