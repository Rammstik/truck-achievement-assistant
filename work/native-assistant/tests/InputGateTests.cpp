// Tests our current-process input interception only. No injected clicks/keys,
// no game process, and no movement/clipping of the real desktop pointer.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cstring>
#include <array>
#include "InputGate.h"
static bool zero(const void* data,size_t length){const auto* p=static_cast<const unsigned char*>(data);for(size_t i=0;i<length;++i)if(p[i])return false;return true;}
template<class Input,class Device> int test_devices(REFIID iid) {
    Input* input=nullptr;
    if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,iid,reinterpret_cast<void**>(&input),nullptr)))return 10;
    Device* mouse=nullptr;Device* keyboard=nullptr;
    if(FAILED(input->CreateDevice(GUID_SysMouse,&mouse,nullptr))||FAILED(input->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr)))return 11;
    mouse->SetDataFormat(&c_dfDIMouse2);keyboard->SetDataFormat(&c_dfDIKeyboard);
    // Unacquired devices give a deterministic baseline without grabbing input
    // from the user's active app. While captured, a successful zero state is
    // intentional: the game must not repeatedly acquire an exclusive mouse.
    DIMOUSESTATE2 before_mouse{};std::array<unsigned char,256> before_keyboard{};
    HRESULT mouse_result=mouse->GetDeviceState(sizeof before_mouse,&before_mouse);
    HRESULT keyboard_result=keyboard->GetDeviceState(sizeof before_keyboard,before_keyboard.data());
    aa::input::Capture(nullptr,false);
    DIMOUSESTATE2 state;std::memset(&state,0xff,sizeof state);
    std::array<unsigned char,256> keys;keys.fill(0xff);
    if(mouse->GetDeviceState(sizeof state,&state)!=DI_OK||!zero(&state,sizeof state))return 12;
    if(keyboard->GetDeviceState(sizeof keys,keys.data())!=DI_OK||!zero(keys.data(),keys.size()))return 13;
    if(mouse->Acquire()!=DI_OK||keyboard->Acquire()!=DI_OK)return 14;
    DWORD count=8;DIDEVICEOBJECTDATA data[8]{};
    if(mouse->GetDeviceData(sizeof data[0],data,&count,0)!=DI_OK||count!=0)return 15;
    count=INFINITE;
    if(keyboard->GetDeviceData(sizeof data[0],nullptr,&count,0)!=DI_OK||count!=0)return 16;
    aa::input::Release();
    if(mouse->GetDeviceState(sizeof state,&state)!=mouse_result)return 17;
    if(keyboard->GetDeviceState(sizeof keys,keys.data())!=keyboard_result)return 18;
    mouse->Release();keyboard->Release();input->Release();return 0;
}
int main(){
    if(!aa::input::Install())return 1;
    int result=test_devices<IDirectInput8W,IDirectInputDevice8W>(IID_IDirectInput8W);if(result)return result;
    result=test_devices<IDirectInput8A,IDirectInputDevice8A>(IID_IDirectInput8A);if(result)return result;
    aa::input::Capture(nullptr,false);
    auto before=aa::input::Snapshot();POINT p{};GetCursorPos(&p);RECT area{};GetClipCursor(&area);
    // These calls would be no-ops at these exact coordinates even if unhooked.
    SetCursorPos(p.x,p.y);ClipCursor(&area);GetAsyncKeyState(VK_LBUTTON);
    auto after=aa::input::Snapshot();
    if(after.cursor_warps_blocked!=before.cursor_warps_blocked+1||after.cursor_clips_blocked!=before.cursor_clips_blocked+1||after.key_polls_blocked!=before.key_polls_blocked+1)return 20;
    {aa::input::OverlayCalls trusted;GetAsyncKeyState(VK_LBUTTON);}
    if(aa::input::Snapshot().key_polls_blocked!=after.key_polls_blocked)return 21;
    aa::input::Release();
    GetAsyncKeyState(VK_LBUTTON);GetCursorPos(&p);
    auto released=aa::input::Snapshot();
    if(released.captured||released.key_polls_blocked!=after.key_polls_blocked||released.mouse_states_blocked!=2||released.keyboard_states_blocked!=2||released.buffered_reads_blocked!=4)return 22;
    aa::input::SetToggleHeld(true);GetAsyncKeyState(VK_F8);
    if(aa::input::Snapshot().key_polls_blocked!=released.key_polls_blocked+1)return 26;
    aa::input::SetToggleHeld(false);
    aa::input::Stop();
    if(aa::input::Snapshot().installed)return 23;
    if(!aa::input::Install())return 24;aa::input::Capture(nullptr,false);aa::input::Stop();
    if(aa::input::Captured())return 25;
    printf("PASS: DirectInput A/W mouse/keyboard states, acquire, buffered reads, Windows polling, overlay bypass, input restore, repeated install/stop.\n");return 0;
}
