// ETS2 polls DirectInput independently of its Win32 message procedure. While
// the panel owns input, release only mouse/keyboard devices (not wheel/pedals),
// prevent immediate reacquisition and return neutral game input. Unacquiring an
// exclusive device gives Windows legacy mouse/text messages back to ImGui.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include "InputGate.h"
#include <dinput.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <thread>
#include <chrono>
namespace aa::input {
namespace {
using Device=IDirectInputDevice8W;
using AcquireFn=HRESULT(STDMETHODCALLTYPE*)(Device*);
using StateFn=HRESULT(STDMETHODCALLTYPE*)(Device*,DWORD,LPVOID);
using DataFn=HRESULT(STDMETHODCALLTYPE*)(Device*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
using WarpFn=BOOL(WINAPI*)(int,int);
using ClipFn=BOOL(WINAPI*)(const RECT*);
using CursorFn=HCURSOR(WINAPI*)(HCURSOR);
using AsyncFn=SHORT(WINAPI*)(int);
using PositionFn=BOOL(WINAPI*)(LPPOINT);
struct Binding{void* target;void* original;int slot;};
std::vector<Binding> bindings;
std::atomic<bool> active{false},installed{false};
std::atomic<bool> toggle_held{false};
std::atomic<unsigned> calls{0};
thread_local bool internal=false;
struct Inside {bool previous=internal;Inside(){internal=true;}~Inside(){internal=previous;}};
struct Call {Call(){++calls;}~Call(){--calls;}};
std::mutex device_mutex;
std::unordered_map<Device*,bool> suspended;
std::mutex cursor_mutex;
RECT saved_clip{};bool has_clip=false;
bool cursor_adjusted=false;
HCURSOR saved_cursor=nullptr;
WarpFn warp=nullptr;ClipFn clip=nullptr;CursorFn cursor=nullptr;AsyncFn async_key=nullptr;
PositionFn position=nullptr;
std::atomic<LONG> frozen_x{0},frozen_y{0};
HMODULE input_module=nullptr;
std::atomic<uint64_t> mouse_states{0},keyboard_states{0},data_reads{0},warps{0},clips{0},polls{0},suspends{0},resumes{0};

void* original(Device* device,int slot) {
    auto target=(*reinterpret_cast<void***>(device))[slot];
    for(const auto& b:bindings)if(b.slot==slot&&b.target==target)return b.original;
    return nullptr;
}
DWORD device_type(Device* device) {
    DIDEVCAPS caps{};caps.dwSize=sizeof caps;
    Inside bypass;
    return SUCCEEDED(device->GetCapabilities(&caps))?GET_DIDEVICE_TYPE(caps.dwDevType):0;
}
bool blocked(Device* device,DWORD& type) {
    if(!active.load()||internal)return false;
    type=device_type(device);
    return type==DI8DEVTYPE_MOUSE||type==DI8DEVTYPE_KEYBOARD;
}
void suspend(Device* device) {
    {std::lock_guard lock(device_mutex);if(!active.load()||suspended.count(device))return;}
    Inside bypass;device->AddRef();
    // Keep a reference until Release(): a game can release its COM pointer
    // while the panel is open. Never retain a dangling device address.
    HRESULT result=device->Unacquire();
    bool kept=false,restore_now=false;
    {
        std::lock_guard lock(device_mutex);
        if(active.load()) {
            auto item=suspended.emplace(device,result==DI_OK);kept=item.second;
            if(!kept)item.first->second=item.first->second||result==DI_OK;
        }else restore_now=result==DI_OK;
    }
    if(restore_now){auto next=reinterpret_cast<AcquireFn>(original(device,7));if(next)next(device);}
    if(!kept)device->Release();else ++suspends;
}
HRESULT STDMETHODCALLTYPE acquire_hook(Device* device) {
    Call call;auto next=reinterpret_cast<AcquireFn>(original(device,7));
    DWORD type=0;
    if(blocked(device,type)){suspend(device);return DI_OK;}
    return next?next(device):DIERR_GENERIC;
}
HRESULT STDMETHODCALLTYPE state_hook(Device* device,DWORD size,LPVOID output) {
    Call call;auto next=reinterpret_cast<StateFn>(original(device,9));
    DWORD type=0;
    if(blocked(device,type)) {
        if(!output||size>4096)return DIERR_INVALIDPARAM;
        suspend(device);std::memset(output,0,size);
        if(type==DI8DEVTYPE_MOUSE)++mouse_states;else ++keyboard_states;
        return DI_OK;
    }
    HRESULT result=next?next(device,size,output):DIERR_GENERIC;
    if(SUCCEEDED(result)&&output&&size==256&&device_type(device)==DI8DEVTYPE_KEYBOARD) {
        auto* keys=static_cast<BYTE*>(output);
        if(toggle_held.load()||(keys[DIK_LCONTROL]&0x80)||(keys[DIK_RCONTROL]&0x80))keys[DIK_F8]=0;
    }
    return result;
}
HRESULT STDMETHODCALLTYPE data_hook(Device* device,DWORD size,LPDIDEVICEOBJECTDATA output,LPDWORD count,DWORD flags) {
    Call call;auto next=reinterpret_cast<DataFn>(original(device,10));
    DWORD type=0;
    if(blocked(device,type)) {
        if(!count)return DIERR_INVALIDPARAM;
        suspend(device);
        // Count zero is the DirectInput contract for an empty buffered read.
        // Do not clear an unknown caller-sized array or mis-handle INFINITE.
        *count=0;++data_reads;return DI_OK;
    }
    HRESULT result=next?next(device,size,output,count,flags):DIERR_GENERIC;
    if(SUCCEEDED(result)&&output&&count&&*count<=4096&&size>=sizeof(DWORD)&&size<=sizeof(DIDEVICEOBJECTDATA)&&
       (toggle_held.load()||(async_key(VK_CONTROL)&0x8000))&&device_type(device)==DI8DEVTYPE_KEYBOARD) {
        auto* bytes=reinterpret_cast<BYTE*>(output);DWORD kept=0;
        for(DWORD i=0;i<*count;++i){DWORD offset=0;std::memcpy(&offset,bytes+i*size,sizeof offset);
            if(offset!=DIK_F8){if(kept!=i)std::memmove(bytes+kept*size,bytes+i*size,size);++kept;}}
        *count=kept;
    }
    return result;
}
BOOL WINAPI warp_hook(int x,int y) {
    Call call;if(active.load()&&!internal){++warps;return TRUE;}
    return warp(x,y);
}
BOOL WINAPI clip_hook(const RECT* rect) {
    Call call;if(active.load()&&!internal){++clips;return TRUE;}
    return clip(rect);
}
HCURSOR WINAPI cursor_hook(HCURSOR value) {
    Call call;
    // ImGui draws a software pointer. The game must not place its own Windows
    // cursor over it, even if it calls SetCursor every frame.
    return cursor(active.load()&&!internal?nullptr:value);
}
SHORT WINAPI async_hook(int key) {
    Call call;if(active.load()&&!internal){++polls;return 0;}
    if(!internal&&key==VK_F8&&(toggle_held.load()||(async_key(VK_CONTROL)&0x8000))){++polls;return 0;}
    return async_key(key);
}
BOOL WINAPI position_hook(LPPOINT output) {
    Call call;
    if(active.load()&&!internal){if(!output)return FALSE;*output={frozen_x.load(),frozen_y.load()};return TRUE;}
    return position(output);
}
bool bind(void* target,void* replacement,int slot,void** destination=nullptr) {
    for(const auto& b:bindings)if(b.target==target)return true;
    void* trampoline=nullptr;
    if(!target||MH_CreateHook(target,replacement,&trampoline)!=MH_OK)return false;
    bindings.push_back({target,trampoline,slot});
    if(destination)*destination=trampoline;
    return true;
}
bool bind_device(Device* device) {
    auto table=*reinterpret_cast<void***>(device);
    return bind(table[7],reinterpret_cast<void*>(acquire_hook),7)&&
           bind(table[9],reinterpret_cast<void*>(state_hook),9)&&
           bind(table[10],reinterpret_cast<void*>(data_hook),10);
}
void remove_bindings() {
    for(const auto& b:bindings)MH_DisableHook(b.target);
    while(calls.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    for(const auto& b:bindings)MH_RemoveHook(b.target);
    bindings.clear();
}
}
bool Install() {
    if(installed.load())return true;
    auto mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED)return false;
    input_module=LoadLibraryExW(L"dinput8.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!input_module)return false;
    using CreateFn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    auto create=reinterpret_cast<CreateFn>(GetProcAddress(input_module,"DirectInput8Create"));
    bool ok=create!=nullptr;
    // The game may use either A/W COM interfaces, and different implementations
    // for mouse and keyboard. Discover every target instead of assuming that
    // one dummy device has the same method bodies as all other devices.
    for(bool wide:{true,false}) {
        if(!ok)break;
        IDirectInput8W* input=nullptr;
        ok=SUCCEEDED(create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,wide?IID_IDirectInput8W:IID_IDirectInput8A,reinterpret_cast<void**>(&input),nullptr));
        if(!ok)break;
        for(const auto& guid:{GUID_SysMouse,GUID_SysKeyboard}) {
            Device* device=nullptr;
            if(FAILED(input->CreateDevice(guid,&device,nullptr))){ok=false;break;}
            ok=bind_device(device);device->Release();if(!ok)break;
        }
        input->Release();
    }
    HMODULE user=GetModuleHandleW(L"user32.dll");
    if(ok)ok=bind(reinterpret_cast<void*>(GetProcAddress(user,"SetCursorPos")),reinterpret_cast<void*>(warp_hook),-1,reinterpret_cast<void**>(&warp))&&
              bind(reinterpret_cast<void*>(GetProcAddress(user,"ClipCursor")),reinterpret_cast<void*>(clip_hook),-1,reinterpret_cast<void**>(&clip))&&
              bind(reinterpret_cast<void*>(GetProcAddress(user,"SetCursor")),reinterpret_cast<void*>(cursor_hook),-1,reinterpret_cast<void**>(&cursor))&&
              bind(reinterpret_cast<void*>(GetProcAddress(user,"GetAsyncKeyState")),reinterpret_cast<void*>(async_hook),-1,reinterpret_cast<void**>(&async_key))&&
              bind(reinterpret_cast<void*>(GetProcAddress(user,"GetCursorPos")),reinterpret_cast<void*>(position_hook),-1,reinterpret_cast<void**>(&position));
    if(ok) {
        for(const auto& b:bindings)if(MH_QueueEnableHook(b.target)!=MH_OK){ok=false;break;}
        if(ok)ok=MH_ApplyQueued()==MH_OK;
    }
    if(!ok){remove_bindings();FreeLibrary(input_module);input_module=nullptr;return false;}
    installed=true;return true;
}
void Capture(HWND window,bool adjust_windows_cursor) {
    if(!installed.load())return;
    const bool was_active=active.exchange(true);
    Inside bypass;
    if(!was_active){POINT point{};if(position(&point)){frozen_x=point.x;frozen_y=point.y;}}
    if(!adjust_windows_cursor)return;
    {std::lock_guard lock(cursor_mutex);
        if(cursor_adjusted)return;
        has_clip=GetClipCursor(&saved_clip)!=FALSE;saved_cursor=GetCursor();cursor_adjusted=true;
    }
    clip(nullptr);
    // Drop the game's Win32 drag capture as well as DirectInput ownership.
    if(GetCapture()==window)ReleaseCapture();
    cursor(nullptr);
}
void Release(bool restore_clip) {
    if(!active.exchange(false))return;
    Inside bypass;
    std::unordered_map<Device*,bool> restore;
    {std::lock_guard lock(device_mutex);restore.swap(suspended);}
    for(const auto& item:restore) {
        Device* device=item.first;
        if(item.second) {
            auto acquire=reinterpret_cast<AcquireFn>(original(device,7));
            if(acquire&&SUCCEEDED(acquire(device))) {
                ++resumes;
                // Discard mouse/keyboard packets accumulated while the panel
                // owned input. Do not replay its clicks to the game.
                auto data=reinterpret_cast<DataFn>(original(device,10));DWORD count=INFINITE;
                if(data)data(device,sizeof(DIDEVICEOBJECTDATA),nullptr,&count,0);
            }
        }
        device->Release();
    }
    {std::lock_guard lock(cursor_mutex);
        if(cursor_adjusted){clip(restore_clip&&has_clip?&saved_clip:nullptr);cursor(saved_cursor);}
        has_clip=false;cursor_adjusted=false;
    }
}
void Stop() {
    if(!installed.load())return;
    Release();remove_bindings();installed=false;toggle_held=false;
    if(input_module){FreeLibrary(input_module);input_module=nullptr;}
}
bool Captured(){return active.load();}
void SetToggleHeld(bool held){toggle_held=held;}
OverlayCalls::OverlayCalls():previous(internal){internal=true;}
OverlayCalls::~OverlayCalls(){internal=previous;}
Diagnostics Snapshot() {
    return {installed.load(),active.load(),mouse_states.load(),keyboard_states.load(),data_reads.load(),
        warps.load(),clips.load(),polls.load(),suspends.load(),resumes.load()};
}
}
