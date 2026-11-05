#pragma once
#include "MapTint.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
namespace aa::mapprobe {
// The engine's ordinary draw loop projects these temporary UI-only entries.
// The original game-owned array is restored before the temporary copy expires.
class MapMarkerScope {
    inline static std::recursive_mutex mutex;
    inline static thread_local bool nested=false;
    std::unique_lock<std::recursive_mutex> lock;
    uintptr_t field=0;
    std::array<uint64_t,2> saved{};
    std::unique_ptr<unsigned char[]> buffer;
    bool installed=false,entered=false;
    static bool read(uintptr_t p,void* out,size_t n){SIZE_T got=0;return p&&p<=UINTPTR_MAX-n&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,n,&got)&&got==n;}
    static bool write(uintptr_t p,const void* in,size_t n){SIZE_T got=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),in,n,&got)&&got==n;}
public:
    MapMarkerScope(void* widget,const std::vector<aa::maptint::Marker>& markers):lock(mutex) {
        if(nested||markers.empty()||markers.size()>8192)return;
        auto p=reinterpret_cast<uintptr_t>(widget);if(!p||p>UINTPTR_MAX-0x3c8)return;
        field=p+0x3b8;if(!read(field,saved.data(),sizeof saved)||saved[1]>16384)return;
        const size_t bytes=static_cast<size_t>(saved[1])*40;
        buffer=std::make_unique<unsigned char[]>(bytes+markers.size()*40);
        if(bytes&&!read(saved[0],buffer.get(),bytes))return;
        for(size_t i=0;i<markers.size();++i){
            auto* entry=buffer.get()+bytes+i*40;memset(entry,0,40);
            const auto& marker=markers[i];float position[3]={marker.x,marker.y,marker.z};uint32_t kind=12;
            memcpy(entry+8,position,12);memcpy(entry+20,&kind,4);
        }
        const std::array<uint64_t,2> replacement={reinterpret_cast<uintptr_t>(buffer.get()),saved[1]+markers.size()};
        nested=true;entered=true;
        if(write(field,replacement.data(),sizeof replacement))installed=true;
        else if(!write(field,saved.data(),sizeof saved))buffer.release();
    }
    ~MapMarkerScope(){if(installed&&!write(field,saved.data(),sizeof saved))buffer.release();if(entered)nested=false;}
    MapMarkerScope(const MapMarkerScope&)=delete;
    MapMarkerScope& operator=(const MapMarkerScope&)=delete;
    bool Installed()const{return installed;}
};
}
