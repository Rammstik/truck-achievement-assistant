#include "../src/MapTint.cpp"
#include <iostream>
#include <stdexcept>
extern "C" uint32_t AAMapTintTestCall(float,float,float,float,uintptr_t);
extern "C" void AAMapTintTestReturn();
namespace {
using namespace aa::maptint;
__declspec(noinline) uint32_t __fastcall fixturePack(float r,float g,float b,float a) {
    return uint32_t(r*255)|(uint32_t(g*255)<<8)|(uint32_t(b*255)<<16)|(uint32_t(a*255)<<24);
}
PackFn volatile packCall=&fixturePack;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
}
int main() {
    using namespace aa::maptint;
    try {
        auto init=MH_Initialize();check(init==MH_OK||init==MH_ERROR_ALREADY_INITIALIZED,"MinHook init");
        hookTarget=reinterpret_cast<void*>(&fixturePack);expectedReturn=reinterpret_cast<uintptr_t>(&AAMapTintTestReturn);
        check(MH_CreateHook(hookTarget,reinterpret_cast<void*>(&AAMapTintBridge),reinterpret_cast<void**>(&original))==MH_OK,"pack hook create");
        check(MH_EnableHook(hookTarget)==MH_OK,"pack hook enable");active=true;
        const json prefs={{"account","a"},{"profile","p"},{"plan",{{"a:p:goal",true}}}};
        json info={{"kind","viewpoints"},{"available",true},{"needed",1},{"items",json::array({{{"x",123.5},{"z",-456.25},{"viewed",false}},{{"x",999.0},{"z",999.0},{"viewed",true}}})}};
        json model={{"account","a"},{"profile","p"},{"achievements",json::array({{{"id","goal"},{"status","progress"}}})},{"details",{{"achievements",{{"goal",info}}}}}};
        SetSelection(model,prefs);check(Status()["targets"]==1,"selected targets wrong");
        std::array<unsigned char,40> entry{};std::array<unsigned char,0x100> frame{};
        uintptr_t address=reinterpret_cast<uintptr_t>(entry.data());memcpy(frame.data(),&address,8);
        uintptr_t frameBase=reinterpret_cast<uintptr_t>(frame.data())+0x80;
        uint32_t kind=30;float position[3]={123.5f,0,-456.25f};memcpy(entry.data()+8,position,12);memcpy(entry.data()+20,&kind,4);
        const auto originalEntry=entry;
        const uint32_t normal=original(1,1,1,0.4f);
        const uint32_t tinted=AAMapTintTestCall(1,1,1,0.4f,frameBase);
        check(tinted!=normal,"matching point unchanged through assembly frame capture");
        check((tinted&0xff000000)==(normal&0xff000000),"alpha changed");check(entry==originalEntry,"map entry mutated");
        check(packCall(0.1f,0.3f,0.6f,0.7f)==original(0.1f,0.3f,0.6f,0.7f),"unrelated color call changed or float ABI broken");
        kind=31;memcpy(entry.data()+20,&kind,4);
        check(AAMapTintTestCall(1,1,1,0.4f,frameBase)==normal,"viewed icon tinted");
        kind=30;memcpy(entry.data()+20,&kind,4);position[0]+=1;memcpy(entry.data()+8,position,12);
        check(AAMapTintTestCall(1,1,1,0.4f,frameBase)==normal,"nearby different point tinted");
        position[0]-=1;memcpy(entry.data()+8,position,12);
        kind=10;memcpy(entry.data()+20,&kind,4);
        const auto& cityLocation=CityPositions[0];position[0]=float(cityLocation.x);position[2]=float(cityLocation.z);memcpy(entry.data()+8,position,12);
        auto cityModel=model;auto& cityInfo=cityModel["details"]["achievements"]["goal"];
        cityInfo={{"kind","visited_cities"},{"needed",1},{"items",json::array({{{"id",cityLocation.city},{"visited",false}}})}};
        SetSelection(cityModel,prefs);check(Status()["city_positions"].get<size_t>()>0,"city positions missing");
        const auto red=AAMapTintTestCall(1,1,1,0.4f,frameBase);
        check(red==original(1,0,0,0.4f),"city pin not bright red or alpha changed");
        cityInfo["items"][0]["visited"]=true;SetSelection(cityModel,prefs);
        check(AAMapTintTestCall(1,1,1,0.4f,frameBase)==normal,"visited city tinted");
        kind=30;position[0]=123.5f;position[2]=-456.25f;memcpy(entry.data()+20,&kind,4);memcpy(entry.data()+8,position,12);

        auto atsModel=model,atsPrefs=prefs;
        atsModel["achievements"]=json::array({{{"id","az_racetrack"},{"status","locked"}}});
        atsModel["details"]["achievements"]["az_racetrack"]={{"kind","tracking"}};
        atsPrefs["plan"]={{"a:p:az_racetrack",true}};
        SetSelection(atsModel,atsPrefs);check(!Markers().empty(),"ATS race track not mapped");
        atsPrefs["plan"]["a:p:az_racetrack"]=false;SetSelection(atsModel,atsPrefs);check(Markers().empty(),"removed ATS road goal persists");
        for(auto goal:{"use_gas_stat","use_weigh_stat","buy_truck","custompj","maxed_garage","wa_use_ferry","wa_aerospace"}){
            atsModel["achievements"]=json::array({{{"id",goal},{"status","locked"}}});atsModel["details"]["achievements"][goal]={{"kind","tracking"}};
            atsPrefs["plan"]={{std::string("a:p:")+goal,true}};
            SetSelection(atsModel,atsPrefs);check(!Markers().empty(),goal);
        }
        atsPrefs["profile"]="different";SetSelection(atsModel,atsPrefs);check(Status()["targets"]==0,"ATS profile leak");
        Stop();MH_Uninitialize();std::cout<<"ATS map targets, actual ASM red/alpha ABI, removal and isolation PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";Stop();return 1;}
}
