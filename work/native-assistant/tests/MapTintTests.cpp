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
        auto triggerModel=model;triggerModel["achievements"][0]["id"]="fr_landmarks";
        triggerModel["details"]["achievements"]["fr_landmarks"]={{"kind","tracking"},{"mode","manual"},{"available",false}};
        auto triggerPrefs=prefs;triggerPrefs["plan"]={{"a:p:fr_landmarks",true}};
        SetSelection(triggerModel,triggerPrefs);check(Markers().size()==6,"six unknown French landmarks not exposed");
        const auto marker=Markers()[0];kind=12;position[0]=marker.x;position[2]=marker.z;
        memcpy(entry.data()+20,&kind,4);memcpy(entry.data()+8,position,12);
        check(AAMapTintTestCall(1,1,1,0.4f,frameBase)==original(1,0,0,0.4f),"landmark marker not red");
        triggerPrefs["plan"]["a:p:fr_landmarks"]=false;SetSelection(triggerModel,triggerPrefs);check(Markers().empty(),"removed landmark persists");
        kind=30;position[0]=123.5f;position[2]=-456.25f;memcpy(entry.data()+20,&kind,4);memcpy(entry.data()+8,position,12);
        auto allModel=model;allModel["achievements"]=json::array();allModel["details"]["achievements"]=json::object();auto allPrefs=prefs;allPrefs["plan"]=json::object();
        std::set<std::string> triggerGoals;for(const auto& t:TriggerPositions)triggerGoals.insert(t.goal);
        for(const auto& goal:triggerGoals){allModel["achievements"].push_back({{"id",goal},{"status","progress"}});allModel["details"]["achievements"][goal]={{"kind","tracking"},{"mode","manual"}};allPrefs["plan"]["a:p:"+goal]=true;}
        SetSelection(allModel,allPrefs);check(triggerGoals.size()==7&&Markers().size()==25,"not all seven trigger groups mapped");
        allPrefs["plan"]=json::object();SetSelection(allModel,allPrefs);check(Markers().empty(),"trigger group markers survive cleared plan");
        auto cityOptionModel=model;cityOptionModel["achievements"]=json::array({{{"id","sc_visit_cities"},{"status","progress"}}});
        cityOptionModel["details"]["achievements"]["sc_visit_cities"]={{"kind","visited_cities"},{"items",json::array({{{"id","aalborg"},{"visited",false}}})}};
        auto cityOptionPrefs=prefs;cityOptionPrefs["plan"]={{"a:p:sc_visit_cities",true}};
        SetSelection(cityOptionModel,cityOptionPrefs);const auto cityTargets=Status()["targets"].get<size_t>();
        check(cityTargets>0&&!aa::offers::Status()["active"].get<bool>(),"default city option affected map or enabled filtering");
        cityOptionPrefs["filter_unvisited_cities"]=true;SetSelection(cityOptionModel,cityOptionPrefs);
        check(Status()["targets"].get<size_t>()==cityTargets&&aa::offers::Status()["active"].get<bool>(),"enabling city filter altered map targets");
        cityOptionPrefs["filter_unvisited_cities"]=false;SetSelection(cityOptionModel,cityOptionPrefs);
        check(Status()["targets"].get<size_t>()==cityTargets&&!aa::offers::Status()["active"].get<bool>(),"disabling city filter removed map targets");
        auto hotelModel=model;hotelModel["achievements"]=json::array({{{"id","gr_hotels"},{"status","progress"}}});
        hotelModel["details"]["achievements"]["gr_hotels"]={{"kind","tracking"},{"items",json::array({{{"id",".gr_hotels.argostoli"},{"current",1},{"target",1}}})}};
        auto hotelPrefs=prefs;hotelPrefs["plan"]={{"a:p:gr_hotels",true}};SetSelection(hotelModel,hotelPrefs);
        check(Status()["company_positions"]==4&&Markers().size()==4,"remaining hotel markers not selected exactly");
        for(const auto& f:FeaturePositions)if(f.kind==1&&std::string(f.company)=="artemia"&&std::string(f.city)=="argostoli")
            for(const auto& m:Markers())check(std::abs(m.x-f.x)>0.05||std::abs(m.z-f.z)>0.05,"completed Argostoli hotel still marked");
        for(const auto& spec:{std::pair<const char*,const char*>{"sc_scania_cargo","scania_fac"},{"sc_volvo_cargo","volvo_fac"}}){
            auto factoryModel=model;factoryModel["achievements"]=json::array({{{"id",spec.first},{"status","locked"}}});
            factoryModel["details"]["achievements"][spec.first]={{"kind","tracking"}};
            auto factoryPrefs=prefs;factoryPrefs["plan"]={{std::string("a:p:")+spec.first,true}};
            SetSelection(factoryModel,factoryPrefs);check(Status()["company_positions"].get<size_t>()>0,"factory source not marked");
            check(Status()["city_positions"].get<size_t>()>0,"factory city not marked");
            bool found=false;
            for(const auto& f:FeaturePositions)if(f.kind==1&&std::string(f.company)==spec.second){
                for(const auto& marker:Markers())if(std::abs(marker.x-f.x)<0.05&&std::abs(marker.z-f.z)<0.05)found=true;
            }
            check(found,"exact source factory marker absent");
            for(const auto& f:FeaturePositions)if(f.kind==1&&std::string(f.company)==(std::string(spec.second)=="scania_fac"?"scania_dlr":"volvo_dlr"))
                for(const auto& marker:Markers())check(std::abs(marker.x-f.x)>0.05||std::abs(marker.z-f.z)>0.05,"dealer confused with factory");
            factoryPrefs["plan"]=json::object();SetSelection(factoryModel,factoryPrefs);check(Status()["targets"]==0,"removed factory/city retained");
        }
                auto services=model;services["achievements"]=json::array({{{"id","use_gas_station"},{"status","locked"}}});services["details"]["achievements"]["use_gas_station"]={{"kind","tracking"}};
        auto servicePrefs=prefs;servicePrefs["plan"]={{"a:p:use_gas_station",true}};SetSelection(services,servicePrefs);
        check(Status()["service_positions"]==3385&&Markers().size()>128&&Markers().size()<=8192,"large service marker set missing or exceeds native cap");
        services["achievements"][0]["id"]="use_all_ports";services["details"]["achievements"]["use_all_ports"]={{"kind","tracking"}};
        const FeaturePosition* port=nullptr;for(const auto& f:FeaturePositions)if(f.kind==3){port=&f;break;}
        check(port!=nullptr,"ferry table empty");services["details"]["map_progress"]["used_ports"]=json::array({port->port});servicePrefs["plan"]={{"a:p:use_all_ports",true}};SetSelection(services,servicePrefs);
        for(const auto& m:Markers())check(std::abs(m.x-port->x)>0.05||std::abs(m.z-port->z)>0.05,"used ferry port retained");
        services["achievements"][0]["id"]="it_garages";services["details"]["achievements"]["it_garages"]={{"kind","tracking"}};
        services["details"]["map_progress"]["garages"]=json::array({{{"city","catania"},{"status",1}}});servicePrefs["plan"]={{"a:p:it_garages",true}};SetSelection(services,servicePrefs);
        check(Status()["service_positions"].get<size_t>()>0,"remaining Sicilian garages missing");
        for(const auto& f:FeaturePositions)if(f.kind==2&&std::string(f.city)=="catania")for(const auto& m:Markers())
            check(std::abs(m.x-f.x)>0.05||std::abs(m.z-f.z)>0.05,"owned Sicilian garage retained");
        auto unplanned=prefs;unplanned["plan"]["a:p:goal"]=false;SetSelection(model,unplanned);
        check(AAMapTintTestCall(1,1,1,0.4f,frameBase)==normal,"removed plan still tinted");
        auto mismatch=prefs;mismatch["profile"]="other";SetSelection(model,mismatch);check(Status()["targets"]==0,"profile leakage");
        model["achievements"][0]["status"]="done";SetSelection(model,prefs);check(Status()["targets"]==0,"completed achievement tinted");
        check(AAMapTintTestCall(1,1,1,0.4f,1)==normal,"invalid frame changed color");
        Stop();check(packCall(1,1,1,0.4f)==normal,"original not restored");
        std::cout<<"PASS: native ASM captures caller frame; only planned unviewed point tinted; alpha/map entry preserved; other callers, viewed/nearby points, removed plan and other profiles unchanged; invalid frame safe; hook removed.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';Stop();return 1;}
}
