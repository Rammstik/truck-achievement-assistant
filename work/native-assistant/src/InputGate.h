#pragma once
#include <windows.h>
#include <cstdint>
namespace aa::input {
struct Diagnostics {
    bool installed=false,captured=false;
    uint64_t mouse_states_blocked=0,keyboard_states_blocked=0,buffered_reads_blocked=0;
    uint64_t cursor_warps_blocked=0,cursor_clips_blocked=0,key_polls_blocked=0;
    uint64_t devices_suspended=0,devices_resumed=0;
};
// ImGui must read the real Windows pointer while game polling gets a frozen
// position. The bypass is local to the current thread and lexical scope.
class OverlayCalls {
    bool previous;
public:
    OverlayCalls();
    ~OverlayCalls();
    OverlayCalls(const OverlayCalls&)=delete;
    OverlayCalls& operator=(const OverlayCalls&)=delete;
};
bool Install();
void Capture(HWND window,bool adjust_windows_cursor=true);
void Release(bool restore_clip=true);
void Stop();
bool Captured();
void SetToggleHeld(bool held);
Diagnostics Snapshot();
}
