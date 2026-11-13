#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "JobProbe.h"
#include "OfferFilter.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <map>
#include <deque>
#include <thread>
#include <chrono>
namespace aa::jobprobe {
namespace {
using json=nlohmann::json;using Address=uintptr_t;
using Predicate=bool(__fastcall*)(Address,Address,Address);
using City=Address(__fastcall*)(Address);
using Destination=Address(__fastcall*)(Address);
using Name=Address(__fastcall*)(Address,Address,int32_t);
Predicate original=nullptr;City city=nullptr;Destination destination=nullptr;Name name=nullptr;
using SpecialPredicate=bool(__fastcall*)(Address,Address);
SpecialPredicate originalSpecial=nullptr;void* specialTarget=nullptr;
using Update=void(__fastcall*)(Address);
using SetStatus=void(__fastcall*)(Address,const char*const*);
Update originalUpdate=nullptr,rebuild=nullptr;SetStatus setStatus=nullptr;
void* updateTarget=nullptr;std::map<Address,uint64_t> screenRevision;
std::atomic<uint64_t> rejectedByPlan{0},rebuildRequests{0};
void* hookTarget=nullptr;Address base=0;
std::atomic<bool> active{false};std::atomic<unsigned> inFlight{0};
std::atomic<uint64_t> calls{0},written{0},errors{0};
std::mutex mutex;std::deque<json> pending;std::set<std::array<Address,3>> seen;
struct ProviderStats {uint64_t calls=0,accepted=0,captured=0,limited=0,window=0,windowSamples=0;};
std::map<Address,ProviderStats> providers;uint64_t totalCaptured=0;
std::map<Address,ProviderStats> specialProviders;
std::atomic<uint64_t> specialCalls{0},specialRejected{0},specialCargoUnreadable{0};
std::filesystem::path journal;std::string reason="disabled";
template<class T> bool read(Address address,T& result){SIZE_T got=0;return address&&address<=UINTPTR_MAX-sizeof(T)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&result,sizeof(T),&got)&&got==sizeof(T);}
bool live(Address unit){int32_t flags=0;return unit&&read(unit+8,flags)&&flags<0;}
std::string hex(uint64_t value){char out[24];snprintf(out,sizeof out,"%016llx",static_cast<unsigned long long>(value));return out;}
json token(Address unit){
    if(!live(unit))return nullptr;
    uint64_t value=0;name(unit,reinterpret_cast<Address>(&value),-1);
    const auto originalValue=value;std::string decoded;
    constexpr char alphabet[]="\0" "0123456789abcdefghijklmnopqrstuvwxyz_";
    for(unsigned i=0;i<12&&value;++i){auto digit=value%38;value/=38;if(!digit)return json{{"hex",hex(originalValue)},{"text_candidate",nullptr}};decoded+=alphabet[digit];}
    return {{"hex",hex(originalValue)},{"text_candidate",value?json():json(decoded)}};
}
json endpoint(Address unit){
    if(!live(unit))return nullptr;
    Address descriptor=0;read(unit+0x68,descriptor);
    auto cityUnit=city(unit);
    return {{"company",token(descriptor)},{"city",token(cityUnit)}};
}
struct Inside {Inside(){++inFlight;}~Inside(){--inFlight;}};
bool captureOffer(Address table,Address source,Address offer,uint64_t window){
    const bool special=source==0;
    std::lock_guard lock(mutex);auto& stats=special?specialProviders[table]:providers[table];
    if(stats.window!=window){stats.window=window;stats.windowSamples=0;for(auto i=seen.begin();i!=seen.end();)if((*i)[0]==table&&(((*i)[1]==0)==special))i=seen.erase(i);else ++i;}
    if(stats.windowSamples>=96||totalCaptured>=20000){++stats.limited;return false;}
    if(!seen.insert({table,source,offer}).second)return false;
    ++stats.windowSamples;++stats.captured;++totalCaptured;return true;
}
Address specialCargo(Address offer){
    Address table=0,getter=0;if(!read(offer,table)||!read(table+0x130,getter))return 0;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(getter),&info,sizeof info)||info.State!=MEM_COMMIT||
       !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return 0;
    // The game's adjacent Special Transport search predicate uses this same
    // cargo getter. Access faults leave metadata unknown, never fabricate it.
    __try{return reinterpret_cast<Address(__fastcall*)(Address)>(getter)(offer);}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
bool __fastcall specialHook(Address task,Address offer){
    Inside inside;++calls;++specialCalls;const bool accepted=originalSpecial(task,offer);
    if(!active.load()||!accepted)return accepted;
    try{
        Address table=0;read(task,table);
        {std::lock_guard lock(mutex);auto& stats=specialProviders[table];++stats.calls;++stats.accepted;}
        const auto window=GetTickCount64()/5000;const auto plan=std::atomic_load(&aa::offers::current);
        const bool capture=captureOffer(table,0,offer,window);if(!capture&&!plan->active)return accepted;
        auto cargo=token(specialCargo(offer));if(cargo.is_null())++specialCargoUnreadable;
        json row={{"schema",4},{"tick_ms",GetTickCount64()},{"capture_window",window},{"task_vtable_rva",hex(table-base)},
            {"special_transport",true},{"source",nullptr},{"target",nullptr},{"cargo",std::move(cargo)},
            {"native_accepted",accepted},{"filter_applied",plan->active},{"plan_revision",plan->revision}};
        const bool allowed=aa::offers::allows(row,*plan);row["allowed_by_plan"]=allowed;if(!allowed){++rejectedByPlan;++specialRejected;}
        if(capture){std::lock_guard lock(mutex);if(pending.size()<2000)pending.push_back(std::move(row));}
        return accepted&&allowed;
    }catch(...){++errors;}return accepted;
}
bool __fastcall hook(Address task,Address source,Address offer){
    Inside inside;++calls;
    const bool accepted=original(task,source,offer);
    if(!active.load())return accepted;
    try {
        Address table=0,cargo=0;
        if(!read(task,table))return accepted;
        {std::lock_guard lock(mutex);auto& stats=providers[table];++stats.calls;if(accepted)++stats.accepted;}
        if(!accepted||!live(source)||!live(offer)||!read(offer+0x58,cargo))return accepted;
        const auto window=GetTickCount64()/5000;
        auto plan=std::atomic_load(&aa::offers::current);const bool capture=captureOffer(table,source,offer,window);
        if(!capture&&!plan->active)return accepted;
        const auto targetCompany=destination(offer+0x18);
        json row={{"schema",4},{"tick_ms",GetTickCount64()},{"capture_window",window},{"task_vtable_rva",hex(table-base)},{"special_transport",false},
            {"source",endpoint(source)},{"target",endpoint(targetCompany)},{"cargo",token(cargo)},
            {"native_accepted",accepted},{"filter_applied",plan->active},{"plan_revision",plan->revision}};
        const bool allowed=aa::offers::allows(row,*plan);
        row["allowed_by_plan"]=allowed;if(!allowed)++rejectedByPlan;
        if(capture){std::lock_guard lock(mutex);if(pending.size()<2000)pending.push_back(std::move(row));}
        return accepted&&allowed;
    }catch(...){++errors;}
    return accepted;
}
void __fastcall updateHook(Address handler){
    Inside inside;originalUpdate(handler);if(!active.load())return;
    try{
        const auto plan=std::atomic_load(&aa::offers::current);bool refresh=false;
        {std::lock_guard lock(mutex);if(screenRevision.size()>128)screenRevision.clear();auto& previous=screenRevision[handler];if(previous!=plan->revision){previous=plan->revision;refresh=true;}}
        if(refresh){rebuild(handler);++rebuildRequests;if(!plan->active){const char* empty="";setStatus(handler,&empty);}}
        if(plan->active){const char* message="Фильтр достижений включён. Если список пуст — подходящих предложений сейчас нет.";setStatus(handler,&message);}
    }catch(...){++errors;}
}
}
void Install(void* imageBase,const std::filesystem::path& session,const Addresses& addresses){
    Stop();base=reinterpret_cast<Address>(imageBase);journal=session/L"job-offers-probe.jsonl";
    calls=written=errors=0;seen.clear();pending.clear();providers.clear();specialProviders.clear();specialCalls=specialRejected=specialCargoUnreadable=0;totalCaptured=0;screenRevision.clear();rejectedByPlan=0;rebuildRequests=0;
    if(!addresses.predicate){reason="signature_mismatch";return;}
    city=reinterpret_cast<City>(addresses.city);destination=reinterpret_cast<Destination>(addresses.destination);name=reinterpret_cast<Name>(addresses.name);
    hookTarget=reinterpret_cast<void*>(addresses.predicate);
    if(MH_CreateHook(hookTarget,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original))!=MH_OK){hookTarget=nullptr;reason="hook_create_failed";return;}
    specialTarget=reinterpret_cast<void*>(addresses.specialPredicate);
    if(MH_CreateHook(specialTarget,reinterpret_cast<void*>(&specialHook),reinterpret_cast<void**>(&originalSpecial))!=MH_OK){specialTarget=nullptr;Stop();reason="special_hook_failed";return;}
    updateTarget=reinterpret_cast<void*>(addresses.update);rebuild=reinterpret_cast<Update>(addresses.rebuild);setStatus=reinterpret_cast<SetStatus>(addresses.setStatus);
    if(MH_CreateHook(updateTarget,reinterpret_cast<void*>(&updateHook),reinterpret_cast<void**>(&originalUpdate))!=MH_OK){updateTarget=nullptr;Stop();reason="refresh_hook_failed";return;}
    active=true;if(MH_EnableHook(hookTarget)!=MH_OK||MH_EnableHook(specialTarget)!=MH_OK||MH_EnableHook(updateTarget)!=MH_OK){Stop();reason="hook_enable_failed";return;}reason="ready";
}
void Stop(){active=false;if(hookTarget)MH_DisableHook(hookTarget);if(specialTarget)MH_DisableHook(specialTarget);if(updateTarget)MH_DisableHook(updateTarget);while(inFlight.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));if(hookTarget)MH_RemoveHook(hookTarget);if(specialTarget)MH_RemoveHook(specialTarget);if(updateTarget)MH_RemoveHook(updateTarget);hookTarget=specialTarget=updateTarget=nullptr;original=nullptr;originalSpecial=nullptr;originalUpdate=nullptr;Flush();}
void Flush(){std::lock_guard lock(mutex);if(pending.empty())return;std::ofstream file(journal,std::ios::app|std::ios::binary);if(!file){++errors;return;}while(!pending.empty()){file<<pending.front().dump()<<'\n';file.flush();if(!file){++errors;return;}pending.pop_front();++written;}}
json Status(){std::lock_guard lock(mutex);auto stats=json::object();for(const auto& item:providers){const auto& p=item.second;stats[hex(item.first-base)]={{"calls",p.calls},{"accepted",p.accepted},{"captured",p.captured},{"sample_limit_skips",p.limited}};}return {{"active",active.load()},{"reason",reason},{"calls",calls.load()},{"offers_recorded",written.load()},{"errors",errors.load()},{"filter_applied",aa::offers::Status()},{"rejected_by_plan",rejectedByPlan.load()},{"special_calls",specialCalls.load()},{"special_rejected_by_plan",specialRejected.load()},{"special_cargo_unreadable",specialCargoUnreadable.load()},{"refresh_requests",rebuildRequests.load()},{"providers",stats},{"record_limit_reached",totalCaptured>=20000}};}
}
