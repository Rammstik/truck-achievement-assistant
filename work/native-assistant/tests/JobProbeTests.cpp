#include "../src/JobProbe.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace aa::jobprobe;
Address targetCompany=0;bool nativeResult=true;unsigned originalCalls=0;
Address specialCargoValue=0;unsigned specialCalls=0;
__declspec(noinline) bool __fastcall nativeSpecial(Address task,Address offer){++specialCalls;return nativeResult&&task&&offer;}
Address __fastcall getSpecialCargo(Address){return specialCargoValue;}
bool __fastcall nativePredicate(Address,Address,Address){++originalCalls;return nativeResult;}
Address __fastcall getCity(Address unit){Address result=0;read(unit+0x100,result);return result;}
Address __fastcall getDestination(Address){return targetCompany;}
Address __fastcall getName(Address unit,Address out,int32_t){uint64_t tokenValue=0;read(unit+0x90,tokenValue);memcpy(reinterpret_cast<void*>(out),&tokenValue,8);return out;}
unsigned updateCalls=0,refreshCalls=0;
void __fastcall fakeUpdate(Address){++updateCalls;}
void __fastcall fakeRefresh(Address){++refreshCalls;}
void __fastcall fakeStatus(Address,const char*const*){}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int wmain(int argc,wchar_t** argv){using namespace aa::jobprobe;try{
    if(argc!=2)throw std::runtime_error("output directory required");
    std::filesystem::create_directories(argv[1]);journal=std::filesystem::path(argv[1])/L"job-offers-probe.jsonl";
    std::array<unsigned char,0x200> source{},dest{},cargoData{},companyDef{},cityData{},offer{};std::array<unsigned char,8> task{};
    for(auto* p:{&source,&dest,&cargoData,&companyDef,&cityData,&offer}){int32_t flags=INT32_MIN;memcpy(p->data()+8,&flags,4);uint64_t value=11;memcpy(p->data()+0x90,&value,8);}
    auto address=[](auto& value){return reinterpret_cast<Address>(value.data());};
    Address def=address(companyDef),cityValue=address(cityData),cargoValue=address(cargoData),table=0x100;
    memcpy(source.data()+0x68,&def,8);memcpy(dest.data()+0x68,&def,8);memcpy(source.data()+0x100,&cityValue,8);memcpy(dest.data()+0x100,&cityValue,8);memcpy(offer.data()+0x58,&cargoValue,8);memcpy(task.data(),&table,8);
    targetCompany=address(dest);original=nativePredicate;city=getCity;destination=getDestination;name=getName;active=true;
    const auto originalSource=source,originalOffer=offer;
    check(hook(address(task),address(source),address(offer)),"native accepted result changed");
    check(pending.size()==1,"offer not captured");check(pending.front()["source"]["company"]["text_candidate"]=="a","token decode fixture");
    check(pending.front()["filter_applied"]==false,"probe applied a filter");
    hook(address(task),address(source),address(offer));check(pending.size()==1,"duplicate offer not bounded");
    providers[table].windowSamples=96;
    hook(address(task),address(source),address(offer));check(pending.size()==1,"provider quota ignored");
    Address otherTable=0x200;memcpy(task.data(),&otherTable,8);
    hook(address(task),address(source),address(offer));check(pending.size()==2,"one provider starves another");
    providers[otherTable].window=0;
    hook(address(task),address(source),address(offer));check(pending.size()==3,"new capture window did not reset quota");
    nativeResult=false;check(!hook(address(task),address(source),address(offer)),"native rejected result changed");
    check(source==originalSource&&offer==originalOffer,"market memory modified");
    Flush();check(written==3&&errors==0,"probe flush failed");check(originalCalls==6,"original predicate call count wrong");
    auto restrictive=std::make_shared<aa::offers::Plan>();restrictive->active=true;restrictive->revision=5;
    restrictive->goals=json::array({{{"class","achievement_each_company_data"},{"attrs",{{"targets",json::array({"b.b"})}}}}});
    std::atomic_store(&aa::offers::current,std::shared_ptr<const aa::offers::Plan>(restrictive));
    nativeResult=true;providers[otherTable].windowSamples=96;
    check(!hook(address(task),address(source),address(offer)),"sample quota bypassed active offer filter");
    originalUpdate=fakeUpdate;rebuild=fakeRefresh;setStatus=fakeStatus;
    updateHook(0x123);updateHook(0x123);check(refreshCalls==1&&updateCalls==2,"screen rebuilt repeatedly at same plan revision");
    auto unfiltered=std::make_shared<aa::offers::Plan>();unfiltered->revision=6;
    std::atomic_store(&aa::offers::current,std::shared_ptr<const aa::offers::Plan>(unfiltered));
    updateHook(0x123);check(refreshCalls==2,"plan removal failed to request refresh");
    check(hook(address(task),address(source),address(offer)),"empty plan failed to restore native result");
    std::array<unsigned char,0x200> specialOffer{},specialTable{};
    auto specialTableValue=address(specialTable),specialGetter=reinterpret_cast<Address>(&getSpecialCargo);
    memcpy(specialOffer.data(),&specialTableValue,8);memcpy(specialTable.data()+0x130,&specialGetter,8);
    specialCargoValue=address(cargoData);originalSpecial=nativeSpecial;
    const auto untouchedSpecial=specialOffer;
    check(specialHook(address(task),address(specialOffer)),"empty plan rejected special transport");
    check(specialProviders[otherTable].captured==1,"ordinary quota starved Special Transport diagnostics");
    std::atomic_store(&aa::offers::current,std::shared_ptr<const aa::offers::Plan>(restrictive));
    check(!specialHook(address(task),address(specialOffer)),"special transport bypassed ordinary filter");
    auto specialPlan=std::make_shared<aa::offers::Plan>();specialPlan->active=true;
    specialPlan->goals=json::array({{{"class","achievement_each_cargo_data"},{"_special",true},{"attrs",{{"cargos",json::array({"a"})}}}}});
    std::atomic_store(&aa::offers::current,std::shared_ptr<const aa::offers::Plan>(specialPlan));
    check(specialHook(address(task),address(specialOffer)),"special cargo getter/decoder failed");
    check(!hook(address(task),address(source),address(offer)),"ordinary offer admitted by special goal");
    nativeResult=false;check(!specialHook(address(task),address(specialOffer)),"native special rejection overridden");nativeResult=true;
    check(specialOffer==untouchedSpecial,"special offer memory changed");
    auto mh=MH_Initialize();check(mh==MH_OK||mh==MH_ERROR_ALREADY_INITIALIZED,"special fixture hook initialization");
    specialTarget=reinterpret_cast<void*>(&nativeSpecial);
    check(MH_CreateHook(specialTarget,reinterpret_cast<void*>(&specialHook),reinterpret_cast<void**>(&originalSpecial))==MH_OK,"special fixture hook create");
    check(MH_EnableHook(specialTarget)==MH_OK,"special fixture hook enable");
    volatile SpecialPredicate callSpecial=nativeSpecial;
    check(callSpecial(address(task),address(specialOffer)),"native two-argument hook ABI changed");
    std::atomic_store(&aa::offers::current,std::shared_ptr<const aa::offers::Plan>(restrictive));
    specialProviders[otherTable].windowSamples=96;
    check(!callSpecial(address(task),address(specialOffer)),"special capture quota bypassed filter");
    MH_DisableHook(specialTarget);MH_RemoveHook(specialTarget);specialTarget=nullptr;
    check(callSpecial(address(task),address(specialOffer)),"special original not restored");
    std::atomic_store(&aa::offers::current,std::shared_ptr<const aa::offers::Plan>(unfiltered));
    active=false;original=nullptr;
    std::cout<<"PASS: ordinary and Special Transport hooks, two-argument native ABI, cargo getter, quotas, union isolation, original rejection and market memory preserved; refresh/restoration.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
