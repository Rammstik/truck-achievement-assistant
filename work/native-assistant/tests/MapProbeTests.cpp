// Exercises the actual probe against synthetic native callbacks, never ETS2.
#include "../src/MapProbe.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace aa::mapprobe;
std::array<unsigned char,128> primitive{};
char materialName[]="/material/ui/map/viewpoint_unseen.mat";
__declspec(noinline) void* __fastcall fixtureMaterial(void*,void*,void*) {
    return primitive.data();
}
MaterialFn volatile materialCall=&fixtureMaterial;
__declspec(noinline) uintptr_t __fastcall fixtureDraw(void* widget,void* context,uintptr_t a3,uintptr_t a4) {
    const char* material=materialName;
    auto result=materialCall(context,widget,&material);
    const float rect[4]={10,20,30,40};const uint32_t color=0xff123456;
    memcpy(static_cast<unsigned char*>(result)+0x10,rect,sizeof rect);
    memcpy(static_cast<unsigned char*>(result)+0x50,&color,sizeof color);
    return a3^a4;
}
DrawFn volatile drawCall=&fixtureDraw;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int wmain(int argc,wchar_t** argv) {
    using namespace aa::mapprobe;
    try {
        if(argc!=2&&argc!=3)throw std::runtime_error("Provide output directory and optional ETS2 executable");
        const std::filesystem::path directory=argv[1];std::filesystem::create_directories(directory);
        {std::ofstream known(directory/L"sha-test.txt",std::ios::binary);known<<"abc";}
        check(fileHash(directory/L"sha-test.txt")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 mismatch");
        // A configured unknown executable must never reach hook creation.
        {std::ofstream config(directory/L"map-probe.json");config<<"{\"enabled\":true}";}
        Install(directory,directory);
        check(!Status()["installed"].get<bool>(),"unknown executable accepted");
#ifdef AA_GAME_ATS
        check(Status()["reason"]=="unsupported_executable","wrong refusal reason");
#else
        check(Status()["reason"]=="signature_mismatch"&&!aa::jobprobe::Status()["active"].get<bool>(),"unknown executable accepted by a feature");
#endif
        check(!drawTarget&&!materialTarget,"targets set for unknown executable");
        check(inspectWidget(reinterpret_cast<void*>(1),nullptr).contains("array_error"),"invalid pointer accepted");
        std::array<unsigned char,0x900> widget{};std::array<unsigned char,0x80> context{};
        std::array<unsigned char,40> item{};
        uintptr_t pointer=reinterpret_cast<uintptr_t>(item.data());uint64_t count=1;
        memcpy(widget.data()+0x3b8,&pointer,8);memcpy(widget.data()+0x3c0,&count,8);
        uint64_t token=0xfedcba9876543210;float position[3]={123.5f,2,-456.25f};uint32_t kind=17;
        memcpy(item.data(),&token,8);memcpy(item.data()+8,position,12);memcpy(item.data()+20,&kind,4);
        const auto originalWidget=widget;const auto originalItem=item;
        check(inspectWidget(widget.data(),context.data())["items"][0]["token_candidate"]=="fedcba9876543210","token precision lost");
        uint64_t invalidCount=UINT64_MAX;memcpy(widget.data()+0x3c0,&invalidCount,8);
        check(inspectWidget(widget.data(),context.data()).contains("array_error"),"unbounded count accepted");
        memcpy(widget.data()+0x3c0,&count,8);
        auto largeWidget=widget;std::vector<unsigned char> largeItems(400*40);
        uintptr_t largePointer=reinterpret_cast<uintptr_t>(largeItems.data());uint64_t largeCount=400;
        memcpy(largeWidget.data()+0x3b8,&largePointer,8);memcpy(largeWidget.data()+0x3c0,&largeCount,8);
        uint32_t viewpointKind=30;memcpy(largeItems.data()+399*40+20,&viewpointKind,4);
        auto fullScan=inspectWidget(largeWidget.data(),context.data());
        check(fullScan["viewpoint_count"]==1&&fullScan["items"][0]["index"]==399,"viewpoint beyond old 256-entry limit omitted");
        {MapMarkerScope scope(widget.data(),{{123,5,456}});
         check(scope.Installed(),"UI marker array not installed");
         uint64_t temporaryCount=0;uintptr_t temporaryPointer=0;
         memcpy(&temporaryCount,widget.data()+0x3c0,8);memcpy(&temporaryPointer,widget.data()+0x3b8,8);
         check(temporaryCount==2,"UI marker count wrong");
         uint32_t markerKind=0;check(copy(temporaryPointer+40+20,&markerKind,4)&&markerKind==12,"UI marker type wrong");
         MapMarkerScope nestedScope(widget.data(),{{1,2,3}});check(!nestedScope.Installed(),"recursive marker duplication");}
        check(widget==originalWidget&&item==originalItem,"UI array not restored");
        {std::vector<aa::maptint::Marker> many(300,{123,5,456});MapMarkerScope scope(widget.data(),many);
         check(scope.Installed(),"expanded service marker array not installed");}
        check(widget==originalWidget&&item==originalItem,"expanded UI marker array not restored");
        {MapMarkerScope invalid(reinterpret_cast<void*>(1),{{1,2,3}});check(!invalid.Installed(),"invalid widget accepted");}
        std::vector<unsigned char> signatureImage(std::max(target::DrawRva+64,target::PackCallRva+5));
        memcpy(signatureImage.data()+target::DrawRva,target::DrawSignature,32);
        memcpy(signatureImage.data()+target::MaterialRva,target::MaterialSignature,32);
        memcpy(signatureImage.data()+target::PackRva,target::PackSignature,32);
        memcpy(signatureImage.data()+target::PackCallRva,target::PackCallSignature,5);
        const auto signatureBase=reinterpret_cast<uintptr_t>(signatureImage.data());
        check(knownMap(signatureBase).draw,"exact signatures rejected");
        signatureImage[target::MaterialRva]^=1;
        check(!knownMap(signatureBase).draw,"mutated material signature accepted");
        signatureImage[target::MaterialRva]^=1;signatureImage[target::DrawRva]^=1;
        check(!knownMap(signatureBase).draw,"mutated draw signature accepted");
#ifndef AA_GAME_ATS
        if(argc==3) {
            HANDLE file=CreateFileW(argv[2],GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            check(file!=INVALID_HANDLE_VALUE,"could not open the game image");
            HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,nullptr);
            CloseHandle(file);
            check(mapping!=nullptr,"could not map the game image");
            auto* image=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
            CloseHandle(mapping);
            check(image!=nullptr,"could not view the game image");
            aa::compatibility::CodeSection section(image);
            const auto map=aa::compatibility::resolveMap(section);
            const auto jobs=aa::compatibility::resolveJobs(section);
            check(map.draw&&jobs.predicate,"map or job bindings did not resolve on the updated executable");
            check(fileHash(argv[2])!=target::Sha256,"test requires a different executable hash");
            const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
            const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
            const auto* sections=IMAGE_FIRST_SECTION(nt);
            const auto* code=std::find_if(sections,sections+nt->FileHeader.NumberOfSections,[](const auto& s){
                return !std::memcmp(s.Name,".text\0\0",8);
            });
            check(code!=sections+nt->FileHeader.NumberOfSections,"missing executable section");
            std::vector<unsigned char> patched(nt->OptionalHeader.SizeOfImage);
            memcpy(patched.data(),image,nt->OptionalHeader.SizeOfHeaders);
            memcpy(patched.data()+code->VirtualAddress,image+code->VirtualAddress,code->Misc.VirtualSize);
            const auto mapOffset=map.draw-reinterpret_cast<uintptr_t>(image);
            const auto jobOffset=jobs.predicate-reinterpret_cast<uintptr_t>(image);
            UnmapViewOfFile(image);
            patched[mapOffset+aa::compatibility::drawRule.size-1]^=1;
            aa::compatibility::CodeSection mapChanged(patched.data());
            check(!aa::compatibility::resolveMap(mapChanged).draw&&aa::compatibility::resolveJobs(mapChanged).predicate,
                  "map change also disabled jobs");
            patched[mapOffset+aa::compatibility::drawRule.size-1]^=1;
            patched[jobOffset+aa::compatibility::predicateRule.size-1]^=1;
            aa::compatibility::CodeSection jobsChanged(patched.data());
            check(aa::compatibility::resolveMap(jobsChanged).draw&&!aa::compatibility::resolveJobs(jobsChanged).predicate,
                  "job change also disabled the map");
            std::cout<<"PASS: updated EXE bindings and independent fallback for changes inside either feature.\n";
        }
#endif
        auto init=MH_Initialize();check(init==MH_OK||init==MH_ERROR_ALREADY_INITIALIZED,"MinHook initialization");
        drawTarget=reinterpret_cast<void*>(&fixtureDraw);materialTarget=reinterpret_cast<void*>(&fixtureMaterial);
        check(MH_CreateHook(drawTarget,reinterpret_cast<void*>(&drawHook),reinterpret_cast<void**>(&originalDraw))==MH_OK,"draw fixture hook");
        check(MH_CreateHook(materialTarget,reinterpret_cast<void*>(&materialHook),reinterpret_cast<void**>(&originalMaterial))==MH_OK,"material fixture hook");
        check(MH_EnableHook(materialTarget)==MH_OK&&MH_EnableHook(drawTarget)==MH_OK,"fixture enable");
        installed=true;stopping=false;
        check(drawCall(widget.data(),context.data(),0xabcd,0x1234)==(0xabcd^0x1234),"unarmed return changed");
        check(sampleCount==0,"unarmed capture");
        Arm();
        check(drawCall(widget.data(),context.data(),0xabcd,0x1234)==(0xabcd^0x1234),"armed return changed");
        check(sampleCount==1,"sample not captured");
        check(widget==originalWidget&&item==originalItem,"probe modified widget or map entry");
        check(pending.front()["primitives"][0]["material"]==materialName,"material not captured");
        check(pending.front()["primitives"][0]["rect_candidate"]==json::array({10.0,20.0,30.0,40.0}),"post-caller rectangle missed");
        check(pending.front()["primitives"][0]["color_candidate"]=="00000000ff123456","original color changed");
        check(currentFrame==nullptr,"TLS frame leaked");
        for(int i=0;i<20;++i)drawCall(widget.data(),context.data(),0,0);
        check(sampleCount==1,"sampling rate not bounded");
        Stop();
        check(written==1&&calls==0&&!installed.load(),"stop did not flush/drain");
        check(drawCall(widget.data(),context.data(),7,3)==4,"original call not restored");
        std::ifstream journal(directory/L"map-probe.jsonl");std::string line;check(bool(std::getline(journal,line)),"journal missing");
        auto record=json::parse(line);check(record["candidate_fields_unverified"]==true,"unverified semantics hidden");
        check(record["items"][0]["position_candidate"][2]==-456.25,"coordinates changed");
        std::cout<<"PASS: unsupported EXE rejected; safe invalid reads/count bounds; real native hooks preserve return and map memory; material and final geometry/color captured; bounded sampling; TLS restored; shutdown flushed and hooks restored.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';Stop();return 1;}
}
