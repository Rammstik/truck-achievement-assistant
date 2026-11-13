#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MapProbe.h"
#include "MapTint.h"
#include "JobProbe.h"
#include "MapMarkerScope.h"
#include "MapProbeTarget.h"
#include "JobProbeTarget.h"
#include "ExecutableCompatibility.h"
#include <windows.h>
#include <wincrypt.h>
#include <MinHook.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace aa::mapprobe {
namespace {
using json=nlohmann::json;
using DrawFn=uintptr_t(__fastcall*)(void*,void*,uintptr_t,uintptr_t);
using MaterialFn=void*(__fastcall*)(void*,void*,void*);
DrawFn originalDraw=nullptr;
MaterialFn originalMaterial=nullptr;
void* drawTarget=nullptr;
void* materialTarget=nullptr;
std::atomic<bool> installed{false},stopping{true};
std::atomic<unsigned> calls{0};
std::atomic<uint64_t> drawCalls{0},materialCalls{0},sampleCount{0},written{0},dropped{0};
std::atomic<ULONGLONG> until{0},lastSample{0};
std::mutex stateMutex;
std::deque<json> pending;
std::string reason="disabled",observedHash,writeError;
std::filesystem::path output;
struct Inside {Inside(){++calls;}~Inside(){--calls;}};
bool copy(uintptr_t address,void* out,size_t bytes) {
    if(!address||bytes>1024*1024||address>UINTPTR_MAX-bytes)return false;
    SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,bytes,&got)&&got==bytes;
}
template<class T> bool field(void* p,size_t offset,T& out) {
    auto address=reinterpret_cast<uintptr_t>(p);
    return address<=UINTPTR_MAX-offset&&copy(address+offset,&out,sizeof out);
}
std::string hex(uint64_t value) {
    char result[24];snprintf(result,sizeof result,"%016llx",static_cast<unsigned long long>(value));return result;
}
std::string textAt(uintptr_t address) {
    std::string out;
    for(size_t i=0;i<192;++i){char c=0;if(!copy(address+i,&c,1))return {};if(!c)return out;if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>126)return {};out+=c;}
    return {};
}
std::string fileHash(const std::filesystem::path& path) {
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return {};
    if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)){CryptReleaseContext(provider,0);return {};}
    std::ifstream input(path,std::ios::binary);std::array<char,65536> buffer{};bool ok=bool(input);
    while(ok&&input){input.read(buffer.data(),buffer.size());auto got=input.gcount();if(got&&!CryptHashData(hash,reinterpret_cast<BYTE*>(buffer.data()),static_cast<DWORD>(got),0))ok=false;}
    if(!input.eof())ok=false;
    std::array<BYTE,32> digest{};DWORD size=static_cast<DWORD>(digest.size());
    ok=ok&&CryptGetHashParam(hash,HP_HASHVAL,digest.data(),&size,0)&&size==digest.size();
    CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    std::string out;if(ok)for(auto byte:digest){char v[3];snprintf(v,sizeof v,"%02x",byte);out+=v;}return out;
}
struct Primitive {void* address;std::string material;};
struct Frame {void* widget;json data;std::vector<Primitive> primitives;unsigned observed=0;};
thread_local Frame* currentFrame=nullptr;
struct FrameScope {Frame* previous=currentFrame;FrameScope(Frame* value){currentFrame=value;}~FrameScope(){currentFrame=previous;}};
json inspectWidget(void* widget,void* context) {
    json out={{"schema",1},{"tick_ms",GetTickCount64()},{"received_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},{"widget_address",hex(reinterpret_cast<uintptr_t>(widget))},
        {"render_context_address",hex(reinterpret_cast<uintptr_t>(context))},{"candidate_fields_unverified",true}};
    uintptr_t start=0;uint64_t count=0;
    if(!field(widget,0x3b8,start)||!field(widget,0x3c0,count)||count>65536){out["array_error"]="unreadable_or_invalid_count";return out;}
    out["reported_count"]=count;out["items"]=json::array();
    const size_t limit=static_cast<size_t>(std::min<uint64_t>(count,16384));
    out["scan_truncated"]=count>limit;out["items_truncated"]=count>256;
    std::vector<unsigned char> entries(limit*40);
    if(limit&&!copy(start,entries.data(),entries.size())){out["array_error"]="unreadable_entries";return out;}
    json histogram=json::object();size_t viewpointCount=0;
    // Full bounded scan first: prioritize viewpoint entries over road-number icons.
    for(int pass=0;pass<2;++pass)for(size_t i=0;i<limit;++i){
        const auto* raw=entries.data()+i*40;
        uint64_t token=0;float position[3]{};uint32_t kind=0,flags=0;
        memcpy(&token,raw,8);memcpy(position,raw+8,12);memcpy(&kind,raw+20,4);memcpy(&flags,raw+24,4);
        const bool viewpoint=kind==30||kind==31;
        if(pass==0){auto key=std::to_string(kind);histogram[key]=histogram.value(key,0)+1;if(viewpoint)++viewpointCount;}
        if((pass==0)!=viewpoint||out["items"].size()>=256)continue;
        out["items"].push_back({{"index",i},{"token_candidate",hex(token)},{"position_candidate",position},{"kind_candidate",kind},{"flags_candidate",flags}});
    }
    out["kind_counts"]=histogram;out["viewpoint_count"]=viewpointCount;
    std::array<float,16> matrix{};
    if(field(widget,0x7dc,matrix))out["matrix_candidate"]=matrix;
    std::array<float,4> rect{};
    if(field(context,0x18,rect))out["context_rect_candidate"]=rect;
    return out;
}
void enqueue(Frame& frame) {
    frame.data["primitives"]=json::array();
    for(const auto& primitive:frame.primitives){
        std::array<float,4> rect{};uint32_t color=0;
        json p={{"material",primitive.material},{"address",hex(reinterpret_cast<uintptr_t>(primitive.address))},
            {"lifetime_unverified",true}};
        if(field(primitive.address,0x10,rect))p["rect_candidate"]=rect;
        if(field(primitive.address,0x50,color))p["color_candidate"]=hex(color);
        frame.data["primitives"].push_back(std::move(p));
    }
    frame.data["material_calls_in_frame"]=frame.observed;
    frame.data["primitives_truncated"]=frame.observed>frame.primitives.size();
    std::lock_guard lock(stateMutex);
    if(pending.size()<8){pending.push_back(std::move(frame.data));++sampleCount;}else ++dropped;
}
uintptr_t __fastcall drawHook(void* widget,void* context,uintptr_t a3,uintptr_t a4) {
    Inside inside;++drawCalls;
    std::unique_ptr<MapMarkerScope> markers;
    try{markers=std::make_unique<MapMarkerScope>(widget,stopping.load()?std::vector<aa::maptint::Marker>{}:aa::maptint::Markers());}catch(...){++dropped;}
    const auto now=GetTickCount64();auto previous=lastSample.load();
    const bool sample=!stopping.load()&&until.load()>now&&now-previous>=1000&&lastSample.compare_exchange_strong(previous,now);
    if(!sample)return originalDraw(widget,context,a3,a4);
    Frame frame;frame.widget=widget;
    try{frame.data=inspectWidget(widget,context);}catch(...){frame.data=json::object();}
    FrameScope scope(&frame);
    const auto result=originalDraw(widget,context,a3,a4);
    try{enqueue(frame);}catch(...){++dropped;}
    return result;
}
void* __fastcall materialHook(void* context,void* widget,void* name) {
    Inside inside;++materialCalls;
    void* result=originalMaterial(context,widget,name);
    try {
        if(!stopping.load()&&currentFrame&&currentFrame->widget==widget){
            ++currentFrame->observed;
            if(result){
                uintptr_t string=0;
                if(field(name,0,string)){
                    auto material=textAt(string);
                    if(material.rfind("/material/ui/map/",0)==0){
                        const bool priority=material.find("/viewpoint")!=std::string::npos||material.find("/city_")!=std::string::npos;
                        if(currentFrame->primitives.size()>=64&&priority){
                            auto& values=currentFrame->primitives;
                            auto replace=std::find_if(values.begin(),values.end(),[](const Primitive& p){return p.material.find("/viewpoint")==std::string::npos&&p.material.find("/city_")==std::string::npos;});
                            if(replace!=values.end())values.erase(replace);
                        }
                        if(currentFrame->primitives.size()<64)currentFrame->primitives.push_back({result,std::move(material)});
                    }
                }
            }
        }
    }catch(...){++dropped;}
    return result;
}
template<size_t N> bool signature(uintptr_t address,const unsigned char (&expected)[N]) {
    std::array<unsigned char,N> bytes{};
    return copy(address,bytes.data(),N)&&!memcmp(bytes.data(),expected,N);
}
compatibility::MapAddresses knownMap(uintptr_t base) {
    if(!signature(base+target::DrawRva,target::DrawSignature) ||
       !signature(base+target::MaterialRva,target::MaterialSignature) ||
       !signature(base+target::PackRva,target::PackSignature) ||
       !signature(base+target::PackCallRva,target::PackCallSignature))return {};
    return {base+target::DrawRva,base+target::MaterialRva,base+target::PackRva,base+target::PackCallRva};
}
jobprobe::Addresses knownJobs(uintptr_t base) {
    using namespace jobprobe;
    if(!signature(base+jobprobe::target::PredicateRva,jobprobe::target::PredicateSignature) ||
       !signature(base+jobprobe::target::SpecialPredicateRva,jobprobe::target::SpecialPredicateSignature) ||
       !signature(base+jobprobe::target::CityRva,jobprobe::target::CitySignature) ||
       !signature(base+jobprobe::target::DestinationRva,jobprobe::target::DestinationSignature) ||
       !signature(base+jobprobe::target::NameRva,jobprobe::target::NameSignature) ||
       !signature(base+jobprobe::target::UpdateRva,jobprobe::target::UpdateSignature) ||
       !signature(base+jobprobe::target::RebuildRva,jobprobe::target::RebuildSignature) ||
       !signature(base+jobprobe::target::SetStatusRva,jobprobe::target::SetStatusSignature))return {};
    return {base+jobprobe::target::PredicateRva,base+jobprobe::target::SpecialPredicateRva,
        base+jobprobe::target::CityRva,base+jobprobe::target::DestinationRva,base+jobprobe::target::NameRva,
        base+jobprobe::target::UpdateRva,base+jobprobe::target::RebuildRva,base+jobprobe::target::SetStatusRva};
}
void removeHooks() {
    if(drawTarget)MH_DisableHook(drawTarget);
    if(materialTarget)MH_DisableHook(materialTarget);
    while(calls.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(drawTarget)MH_RemoveHook(drawTarget);
    if(materialTarget)MH_RemoveHook(materialTarget);
    drawTarget=materialTarget=nullptr;originalDraw=nullptr;originalMaterial=nullptr;installed=false;
}
}
void Install(const std::filesystem::path& pluginDirectory,const std::filesystem::path& sessionDirectory) {
    Stop();
    {std::lock_guard lock(stateMutex);reason="disabled";observedHash.clear();writeError.clear();pending.clear();output=sessionDirectory/L"map-probe.jsonl";}
    drawCalls=materialCalls=sampleCount=written=dropped=0;until=lastSample=0;
    try {
        std::ifstream file(pluginDirectory/L"map-probe.json");
        if(!file)return;
        auto config=json::parse(file,nullptr,false);
        if(!config.is_object()||!config.value("enabled",false))return;
        wchar_t path[32768]{};auto length=GetModuleFileNameW(nullptr,path,32768);
        if(!length||length>=32768){reason="executable_path_error";return;}
        observedHash=fileHash(path);
        auto base=GetModuleHandleW(nullptr);
        const auto image=reinterpret_cast<uintptr_t>(base);
        compatibility::MapAddresses map{};
        jobprobe::Addresses jobs{};
        if(observedHash==target::Sha256) {
            map=knownMap(image);
            jobs=knownJobs(image);
        }else{
#ifdef AA_GAME_ATS
            reason="unsupported_executable";return;
#else
            const compatibility::CodeSection code(base);
            map=compatibility::resolveMap(code);
            jobs=compatibility::resolveJobs(code);
#endif
        }
        aa::jobprobe::Install(base,sessionDirectory,jobs);
        if(!map.draw){reason="signature_mismatch";return;}
        drawTarget=reinterpret_cast<void*>(map.draw);
        materialTarget=reinterpret_cast<void*>(map.material);
        const bool created=MH_CreateHook(drawTarget,reinterpret_cast<void*>(&drawHook),reinterpret_cast<void**>(&originalDraw))==MH_OK&&
            MH_CreateHook(materialTarget,reinterpret_cast<void*>(&materialHook),reinterpret_cast<void**>(&originalMaterial))==MH_OK;
        if(!created){removeHooks();reason="hook_create_failed";return;}
        stopping=false;
        if(MH_QueueEnableHook(materialTarget)!=MH_OK||MH_QueueEnableHook(drawTarget)!=MH_OK||MH_ApplyQueued()!=MH_OK){stopping=true;removeHooks();reason="hook_enable_failed";return;}
        installed=true;reason="ready";
        if(config.value("highlight_viewpoints",false)&&!aa::maptint::Install(map.pack,map.packCall)){
            stopping=true;removeHooks();reason="recoloring_failed";
        }
    }catch(...){stopping=true;removeHooks();reason="installation_error";}
}
void Stop(){aa::jobprobe::Stop();aa::maptint::Stop();stopping=true;until=0;removeHooks();Flush();}
void Arm(){if(installed.load()){lastSample=0;until=GetTickCount64()+90000;}}
void Flush() {
    std::lock_guard lock(stateMutex);
    if(pending.empty())return;
    std::ofstream file(output,std::ios::binary|std::ios::app);
    if(!file){writeError="cannot_open_map_journal";return;}
    while(!pending.empty()){
        file<<pending.front().dump()<<'\n';file.flush();
        if(!file){writeError="cannot_write_map_journal";return;}
        pending.pop_front();++written;
    }
    writeError.clear();
}
json Status() {
    std::lock_guard lock(stateMutex);
    return {{"installed",installed.load()},{"reason",reason},{"expected_exe_sha256",target::Sha256},{"observed_exe_sha256",observedHash},
        {"recording",installed.load()&&until.load()>GetTickCount64()},{"draw_calls",drawCalls.load()},{"material_calls",materialCalls.load()},
        {"samples",sampleCount.load()},{"written",written.load()},{"dropped",dropped.load()},{"queued",pending.size()},
        {"error",writeError},{"journal",output.u8string()},{"recoloring",aa::maptint::Status()}};
}
}
