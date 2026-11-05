#include "GameConfig.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Overlay.h"
#include "InputGate.h"
#include "MapProbe.h"
#include "MapTint.h"
#include "JobProbe.h"
#include "OfferFilter.h"
#include <d3d11.h>
#include <dxgi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <MinHook.h>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
#include <chrono>
#include <deque>
using json=nlohmann::json;
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace {
std::recursive_mutex ui_mutex;
std::mutex data_mutex,event_mutex;
std::atomic<bool> stopping{false},paused{true};
std::atomic<unsigned> active_presents{0};
std::atomic<uint64_t> gameplay_serial{0};
std::atomic<uint64_t> presented_frames{0},drawn_frames{0};
std::atomic<bool> renderer_ready{false},panel_active{false};
std::thread reader;
Present original_present=nullptr;
void* present_target=nullptr;
HWND game_window=nullptr;
WNDPROC original_wndproc=nullptr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
IDXGISwapChain* active_swap=nullptr;
bool ready=false,visible=false;
ImFont* heading_font=nullptr;
bool toggle_held=false;
bool toggle_keyup_pending=false;
json model=json::object(),preferences=json::object();
std::string runtime_error,last_event,selected_id;
fs::path session,settings_path,plugin_dir;
std::string string_value(const json& obj,const char* key,const std::string& fallback);
HANDLE child=nullptr;
std::deque<json> telemetry_queue;
json telemetry_configuration=json::object();
uint64_t telemetry_sequence=0,telemetry_written=0,telemetry_dropped=0;
std::string telemetry_error,telemetry_run;
fs::path telemetry_journal;
void capture_telemetry(const char* name,const char* payload,bool configuration) {
    if(!name)return;
    auto attributes=json::parse(payload?payload:"[]",nullptr,false);
    if(!attributes.is_array())return;
    std::string account,profile;
    {std::lock_guard lock(ui_mutex);account=string_value(preferences,"account","");profile=string_value(preferences,"profile","");}
    std::lock_guard lock(event_mutex);
    if(configuration)telemetry_configuration[name]=attributes;
    auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    json record={{"schema",1},{"run",telemetry_run},{"sequence",++telemetry_sequence},{"received_unix_ms",now},
        {"kind",configuration?"configuration":"gameplay"},{"name",name},{"attributes",attributes},
        {"selected_account",account},{"selected_profile",profile},{"identity_verified",false}};
    if(!configuration)record["configuration"]=telemetry_configuration;
    if(telemetry_queue.size()<4096)telemetry_queue.push_back(std::move(record));else ++telemetry_dropped;
}
void flush_telemetry() {
    std::lock_guard lock(event_mutex);
    if(telemetry_queue.empty())return;
    std::ofstream out(telemetry_journal,std::ios::app|std::ios::binary);
    if(!out){telemetry_error="Не удалось открыть журнал телеметрии";return;}
    while(!telemetry_queue.empty()) {
        out<<telemetry_queue.front().dump()<<"\n";out.flush();
        if(!out){telemetry_error="Ошибка записи журнала телеметрии";return;}
        telemetry_queue.pop_front();++telemetry_written;
    }
    telemetry_error.clear();
}

uint64_t refresh_id=0;
char search[192]{};
int filter=0;
ULONGLONG first_render=0;

std::string string_value(const json& obj,const char* key,const std::string& fallback="") {
    auto i=obj.find(key);return i!=obj.end()&&i->is_string()?i->get<std::string>():fallback;
}
std::string utf8(const std::wstring& value) {
    if(value.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;
}
std::wstring lower(const std::string& value) {
    if(value.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n);
    CharLowerBuffW(out.data(),static_cast<DWORD>(out.size()));return out;
}
json read_json(const fs::path& path) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return json::object();
    LARGE_INTEGER size{};std::string content;
    if(GetFileSizeEx(f,&size)&&size.QuadPart>=2&&size.QuadPart<=8*1024*1024) {
        content.resize(static_cast<size_t>(size.QuadPart));DWORD got=0;
        if(!ReadFile(f,content.data(),static_cast<DWORD>(content.size()),&got,nullptr)||got!=content.size())content.clear();
    }
    CloseHandle(f);auto result=json::parse(content,nullptr,false);
    return result.is_object()?result:json::object();
}
bool write_json(const fs::path& path,const json& value) {
    auto temp=path;temp+=L".native.tmp";
    auto content=value.dump();HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD wrote=0;bool ok=WriteFile(f,content.data(),static_cast<DWORD>(content.size()),&wrote,nullptr)&&wrote==content.size();
    CloseHandle(f);
    return ok&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
void update_map_selection() {
    std::unique_lock uiLock(ui_mutex,std::try_to_lock);if(!uiLock.owns_lock())return;std::lock_guard dataLock(data_mutex);
    aa::maptint::SetSelection(model,preferences);
}
void save_control(bool stop=false) {
    update_map_selection();
    write_json(settings_path,preferences);
    write_json(session/L"control.json",{{"account",string_value(preferences,"account")},{"profile",string_value(preferences,"profile")},
               {"refresh",++refresh_id},{"stop",stop}});
}
void text(const std::string& value) { ImGui::TextWrapped("%s",value.c_str()); }
void dim(const std::string& value) { ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.64f,.72f,.75f,1));text(value);ImGui::PopStyleColor(); }
std::string number(const json& value) {
    if(value.is_null())return "?";
    if(value.is_number_integer())return std::to_string(value.get<int64_t>());
    if(value.is_number()){char b[64];snprintf(b,sizeof b,"%.2f",value.get<double>());return b;}
    return "?";
}
std::string status_name(const std::string& value) {
    if(value=="done")return "Получено";
    if(value=="progress")return "В процессе";
    if(value=="locked")return "Не получено";
    return "Нет данных";
}
std::string plan_key(const json& current,const json& a) {
    return string_value(current,"account")+":"+string_value(current,"profile")+":"+string_value(a,"id");
}
bool pinned(const std::string& key) {
    if(!preferences.contains("plan")||!preferences["plan"].is_object())return false;
    auto i=preferences["plan"].find(key);return i!=preferences["plan"].end()&&i->is_boolean()&&i->get<bool>();
}
void render_details(const json& a,const json& info) {
    ImGui::SeparatorText("Условие");text(string_value(a,"action"));
    if(a.contains("guidance")&&a["guidance"].is_array()&&!a["guidance"].empty()) {
        ImGui::Spacing();
        if(ImGui::BeginTable("execution_guidance",2,ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Условие",ImGuiTableColumnFlags_WidthFixed,120);
            ImGui::TableSetupColumn("Подробности",ImGuiTableColumnFlags_WidthStretch);
            for(const auto& line:a["guidance"]) {
                ImGui::TableNextRow();ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.74f,.39f,1));text(string_value(line,"label"));ImGui::PopStyleColor();
                ImGui::TableNextColumn();text(string_value(line,"text"));
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
    }
    if(info.empty()){dim("Выберите игровой профиль для проверки выполненных пунктов.");return;}
    std::string kind=string_value(info,"kind");
    if(kind=="truck_brands") {
        ImGui::SeparatorText("Пробег по маркам · оценка");
        if(!info.value("available",false)){dim(string_value(info,"reason"));return;}
        dim("Оценка по журналу заказов. Отдельные отметки Steam не найдены. История может быть неполной; пробег достижения может отличаться. Нет записей — статус неизвестен.");
        text("Марок с записанным пробегом от 999 км: "+number(info["observed_completed"])+". Цель: 5 из 7.");
        if(a.contains("current")&&a["current"].is_number()) {
            const int observed=std::min(info["observed_completed"].get<int>(),5);
            dim(a["current"].get<double>()==observed?"Общее число совпадает со Steam; это не подтверждает каждую марку.":"Число отличается от Steam. Проверьте профиль и время сохранения.");
        }
        std::string candidates;
        if(ImGui::BeginTable("brands",3,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Марка");ImGui::TableSetupColumn("Км в журнале");ImGui::TableSetupColumn("Сведения");ImGui::TableHeadersRow();
            for(const auto& row:info["items"]) {
                ImGui::TableNextRow();ImGui::TableNextColumn();text(string_value(row,"name"));ImGui::TableNextColumn();
                text(row["logged_km"].is_null()?"—":number(row["logged_km"]));ImGui::TableNextColumn();
                bool threshold=row.value("threshold_seen",false);
                text(threshold?"Порог виден в журнале":row["logged_km"].is_null()?"Статус неизвестен":"Порог пока не виден в журнале");
                if(!threshold){if(!candidates.empty())candidates+=", ";candidates+=string_value(row,"name");}
            }
            ImGui::EndTable();
        }
        if(!candidates.empty())text("Кандидаты для проверки: "+candidates+". Выполняйте заказы на собственном грузовике; ориентир — 999 км на марку. После сохранения проверьте Steam-счётчик.");
        dim("Учтено заказов: "+number(info["counted_jobs"])+"; исключено: "+number(info["excluded_records"])+"; неизвестный формат: "+number(info["unreadable_records"])+"; владение/марка не подтверждены: "+number(info["unverified_records"]));
    } else if(kind=="visited_cities"||kind=="transported_cargos") {
        ImGui::SeparatorText(kind=="visited_cities"?"Города выбранного профиля":"Грузы выбранного профиля");
        text("Отмечено: "+number(info["completed"])+"; нужно ещё: "+number(info["needed"]));
        if(a.contains("current")&&a["current"].is_number()&&
           a["current"].get<double>()!=std::min(info["completed"].get<int>(),info["target"].get<int>()))
            text("Счётчик Steam отличается от выбранного сохранения. Возможны другой профиль или задержка записи.");
        if(ImGui::BeginTable("checklist",2,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Пункт");ImGui::TableSetupColumn("В сохранении");ImGui::TableHeadersRow();
            for(const auto& row:info["items"]) {
                ImGui::TableNextRow();ImGui::TableNextColumn();text(string_value(row,"name"));
                ImGui::TableNextColumn();text(row.value("visited",false)?"Отмечен":"Не отмечен");
            }
            ImGui::EndTable();
        }
    }
    else {
        ImGui::SeparatorText(string_value(info,"title","Оставшиеся пункты").c_str());
        for(const char* key : {"reason","action_hint"}) {
            auto value=string_value(info,key); if(!value.empty())text(value);
        }
        if(info.value("estimated",false)||info.value("mode",std::string())=="history"||info.value("mode",std::string())=="sequence")dim("По журналу: найденные записи учтены, неизвестные пункты остаются.");
        for(const char* key : {"observed","observed_completed","observed_deliveries","completed"}) {
            if(info.contains(key)) {
                const bool same=a["current"].is_number()&&info[key].is_number()&&a["current"]==info[key]&&a["target"]==info.value("target",json());
                if(!same)text("Найдено: "+number(info[key])+" / "+number(info.value("target",json())));break;
            }
        }
        const auto summary=string_value(info,"summary");if(!summary.empty())text(summary);
        if(info.contains("items")&&info["items"].is_array()) {
            if(ImGui::BeginTable("extended_details",2,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Пункт",ImGuiTableColumnFlags_WidthStretch,.72f);ImGui::TableSetupColumn("Данные",ImGuiTableColumnFlags_WidthStretch,.28f);ImGui::TableHeadersRow();
                for(const auto& row:info["items"]) {
                    ImGui::TableNextRow();ImGui::TableNextColumn();text(string_value(row,"name"));
                    const auto requirements=string_value(row,"requirements");if(!requirements.empty())dim(requirements);
                    ImGui::TableNextColumn();
                    if(row.value("manual",false))dim("Нет данных");
                    else if(row.value("blocked",false))dim("Позже в цепочке");
                    else if(row.contains("current")) {
                        if(row["current"].is_null())dim("Неизвестно");
                        else {const auto value=number(row["current"])+" / "+number(row.value("target",json()));
                            if(aa::offers::completedItem(row))ImGui::TextColored(ImVec4(.40f,.83f,.65f,1),"%s",value.c_str());else text(value);}
                    }
                    else if(row.contains("viewed")&&row["viewed"].is_boolean())text(row["viewed"].get<bool>()?"Просмотрено":"Не отмечено в сохранении");
                    else if(row.contains("logged")&&row["logged"].is_boolean())text(row["logged"].get<bool>()?"Есть запись в журнале":"Нет записи в журнале");
                    else if(row.contains("deliveries")) {const auto& v=row["deliveries"];text("Записей: "+(v.is_array()?std::to_string(v.size()):number(v)));}
                    else text("Нет данных");
                }
                ImGui::EndTable();
            }
        }
    }
}

#include "OverlayPanel.inl"
LRESULT CALLBACK window_proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    std::lock_guard lock(ui_mutex);
    if((msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN)&&w==VK_F8&&((GetKeyState(VK_CONTROL)&0x8000)||toggle_held)) {
        if(!(l&(1LL<<30))&&!toggle_held) {
            visible=!visible;
            if(visible)aa::input::Capture(h,GetForegroundWindow()==h);else aa::input::Release();
            if(ready){ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();}
        }
        toggle_held=true;toggle_keyup_pending=true;aa::input::SetToggleHeld(true);
        panel_active=visible;
        return 0;
    }
    if((msg==WM_KEYUP||msg==WM_SYSKEYUP)&&w==VK_F8&&toggle_keyup_pending){toggle_held=false;toggle_keyup_pending=false;aa::input::SetToggleHeld(false);return 0;}
    if((msg==WM_ACTIVATEAPP&&!w)||msg==WM_KILLFOCUS) {
        visible=false;toggle_held=false;toggle_keyup_pending=false;aa::input::SetToggleHeld(false);aa::input::Release(false);
        panel_active=false;
        if(ready){ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();}
    }
    if(ready&&visible) {
        {aa::input::OverlayCalls trusted;ImGui_ImplWin32_WndProcHandler(h,msg,w,l);}
        // Capture the whole overlay session, including its first frame and
        // clicks outside the panel. WantCapture* describes the previous frame
        // and cannot keep the game's independent DirectInput polling neutral.
        if(msg>=WM_MOUSEFIRST&&msg<=WM_MOUSELAST)return 0;
        if(msg==WM_SETCURSOR)return TRUE;
        if(msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_CHAR||msg==WM_UNICHAR||msg==WM_SYSKEYDOWN||msg==WM_SYSKEYUP||msg==WM_SYSCHAR)return 0;
        if(msg==WM_INPUT) {
            RAWINPUTHEADER header{};UINT bytes=sizeof header;
            if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_HEADER,&header,&bytes,sizeof header)!=UINT(-1)&&
               (header.dwType==RIM_TYPEMOUSE||header.dwType==RIM_TYPEKEYBOARD))
                return DefWindowProcW(h,msg,w,l); // required raw-input cleanup
        }
    }
    return CallWindowProcW(original_wndproc,h,msg,w,l);
}
bool initialize_gui(IDXGISwapChain* swap) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if(FAILED(swap->GetDesc(&desc))||!desc.OutputWindow)return false;
    DWORD pid=0;GetWindowThreadProcessId(desc.OutputWindow,&pid);
    if(pid!=GetCurrentProcessId()||FAILED(swap->GetDevice(IID_PPV_ARGS(&device))))return false;
    device->GetImmediateContext(&context);game_window=desc.OutputWindow;
    ImGui::CreateContext();list_reveal_id.clear();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);
    const auto font=utf8((fs::path(windows)/L"Fonts/segoeui.ttf").wstring());
    static const ImWchar glyphs[]={0x0020,0x00FF,0x0400,0x052F,0x2000,0x206F,0x2190,0x21FF,0x2DE0,0x2DFF,0xA640,0xA69F,0};
    io.Fonts->AddFontFromFileTTF(font.c_str(),19,nullptr,glyphs);
    const auto bold=fs::path(windows)/L"Fonts/segoeuib.ttf";heading_font=fs::exists(bold)?io.Fonts->AddFontFromFileTTF(utf8(bold.wstring()).c_str(),22,nullptr,glyphs):nullptr;
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowRounding=12;style.ChildRounding=8;style.FrameRounding=6;style.PopupRounding=9;
    style.WindowPadding=ImVec2(20,16);style.FramePadding=ImVec2(10,6);style.ItemSpacing=ImVec2(10,8);style.CellPadding=ImVec2(10,7);style.ScrollbarSize=13;
    style.Colors[ImGuiCol_Text]=ImVec4(.92f,.95f,.97f,1);style.Colors[ImGuiCol_WindowBg]=ImVec4(.055f,.085f,.12f,.99f);
    style.Colors[ImGuiCol_ChildBg]=ImVec4(.043f,.068f,.095f,.93f);style.Colors[ImGuiCol_PopupBg]=ImVec4(.075f,.11f,.15f,1);
    style.Colors[ImGuiCol_TitleBg]=ImVec4(.065f,.10f,.145f,1);style.Colors[ImGuiCol_TitleBgActive]=ImVec4(.11f,.18f,.25f,1);
    style.Colors[ImGuiCol_Border]=ImVec4(.22f,.31f,.39f,.75f);style.Colors[ImGuiCol_Separator]=ImVec4(.22f,.31f,.39f,.65f);
    style.Colors[ImGuiCol_FrameBg]=ImVec4(.10f,.17f,.23f,1);style.Colors[ImGuiCol_FrameBgHovered]=ImVec4(.14f,.23f,.31f,1);
    style.Colors[ImGuiCol_Button]=ImVec4(.15f,.28f,.39f,1);style.Colors[ImGuiCol_ButtonHovered]=ImVec4(.21f,.39f,.53f,1);style.Colors[ImGuiCol_ButtonActive]=ImVec4(.25f,.46f,.61f,1);
    style.Colors[ImGuiCol_Header]=ImVec4(.14f,.25f,.34f,1);style.Colors[ImGuiCol_HeaderHovered]=ImVec4(.18f,.32f,.43f,1);style.Colors[ImGuiCol_HeaderActive]=ImVec4(.22f,.38f,.49f,1);
    style.Colors[ImGuiCol_CheckMark]=ImVec4(.98f,.76f,.39f,1);style.Colors[ImGuiCol_PlotHistogram]=ImVec4(.92f,.70f,.36f,1);
    style.Colors[ImGuiCol_TableHeaderBg]=ImVec4(.10f,.17f,.23f,1);style.Colors[ImGuiCol_TableRowBgAlt]=ImVec4(.12f,.18f,.24f,.32f);
    if(!ImGui_ImplWin32_Init(game_window)||!ImGui_ImplDX11_Init(device.Get(),context.Get()))return false;
    SetLastError(0);original_wndproc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));
    if(!original_wndproc)return false;
    active_swap=swap;ready=true;renderer_ready=true;first_render=GetTickCount64();return true;
}
HRESULT STDMETHODCALLTYPE on_present(IDXGISwapChain* swap,UINT sync,UINT flags) {
    struct Active {Active(){++active_presents;}~Active(){--active_presents;}} active;
    const auto next=original_present;
    ++presented_frames;
    if(!stopping.load()&&!(flags&DXGI_PRESENT_TEST)) {
        std::lock_guard lock(ui_mutex);
        if((!ready&&initialize_gui(swap))||(ready&&swap==active_swap)) {
            // Exclusive DirectInput keyboard mode may not generate WM_KEYDOWN.
            // Poll only our game window, bypassing the game's neutralised API.
            if(GetForegroundWindow()==game_window) {
                bool control=false,f8=false;
                {aa::input::OverlayCalls trusted;control=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;f8=(GetAsyncKeyState(VK_F8)&0x8000)!=0;}
                if(control&&f8&&!toggle_held){
                    visible=!visible;toggle_held=true;toggle_keyup_pending=true;aa::input::SetToggleHeld(true);
                    ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();
                }
                if(!f8){toggle_held=false;aa::input::SetToggleHeld(false);}
            }
            if(visible)aa::input::Capture(game_window,GetForegroundWindow()==game_window);else aa::input::Release();
            if(IsWindowVisible(game_window)&&!IsIconic(game_window)) {
                ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> target;
                if(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)))&&SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target))) {
                    ImGui_ImplDX11_NewFrame();
                    {aa::input::OverlayCalls trusted;ImGui_ImplWin32_NewFrame();}
                    ImGui::NewFrame();draw_panel();ImGui::Render();
                    panel_active=visible;
                    if(!visible)aa::input::Release(); // panel's close button
                    ID3D11RenderTargetView* previous[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ComPtr<ID3D11DepthStencilView> depth;
                    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,previous,&depth);
                    ID3D11RenderTargetView* output=target.Get();context->OMSetRenderTargets(1,&output,nullptr);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,previous,depth.Get());
                    for(auto* view:previous)if(view)view->Release();
                    ++drawn_frames;
                }
            }
        }
    }
    return next(swap,sync,flags);
}
bool install_hook() {
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"AA_DX11_Probe";
    RegisterClassW(&cls);HWND window=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return false;
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> probe_device;ComPtr<ID3D11DeviceContext> probe_context;ComPtr<IDXGISwapChain> probe_swap;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&probe_swap,&probe_device,nullptr,&probe_context);
    if(FAILED(hr))hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&probe_swap,&probe_device,nullptr,&probe_context);
    bool ok=false;
    if(SUCCEEDED(hr)) {
        present_target=(*reinterpret_cast<void***>(probe_swap.Get()))[8];
        auto mh=MH_Initialize();
        if((mh==MH_OK||mh==MH_ERROR_ALREADY_INITIALIZED)&&MH_CreateHook(present_target,on_present,reinterpret_cast<void**>(&original_present))==MH_OK) {
            ok=MH_EnableHook(present_target)==MH_OK;
            if(!ok)MH_RemoveHook(present_target);
        }
    }
    probe_swap.Reset();probe_context.Reset();probe_device.Reset();DestroyWindow(window);UnregisterClassW(cls.lpszClassName,cls.hInstance);return ok;
}
void read_loop() {
    const auto executable=plugin_dir/L"AchievementAssistant/AADataWorker.exe";
    std::wstring command=L"\""+executable.wstring()+L"\" --game "+aa::game::IdWide+L" --parent "+std::to_wstring(GetCurrentProcessId())+L" --session \""+session.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&process)) {
        std::lock_guard lock(data_mutex);runtime_error="Не удалось запустить модуль чтения. Скопируйте папку AchievementAssistant рядом с DLL.";return;
    }
    child=process.hProcess;CloseHandle(process.hThread);
    auto last_stamp=fs::file_time_type::min();
    uint64_t last_serial=0;
    ULONGLONG last_diagnostic=0;
    while(!stopping.load()) {
        try {
            flush_telemetry();
            aa::mapprobe::Flush();
            aa::jobprobe::Flush();
            if(GetTickCount64()-last_diagnostic>=1000) {
                json diagnostic={{"version",aa::game::Version},{"renderer_ready",renderer_ready.load()},{"panel_visible",panel_active.load()},{"paused",paused.load()},
                    {"present_frames",presented_frames.load()},{"drawn_frames",drawn_frames.load()}};
                {std::lock_guard lock(event_mutex);diagnostic["telemetry"]={{"written",telemetry_written},{"queued",telemetry_queue.size()},{"dropped",telemetry_dropped},{"error",telemetry_error},{"journal",utf8(telemetry_journal.wstring())}};}
                diagnostic["map_probe"]=aa::mapprobe::Status();
                diagnostic["job_probe"]=aa::jobprobe::Status();
                const auto input=aa::input::Snapshot();
                diagnostic["input"]={{"installed",input.installed},{"captured",input.captured},{"mouse_states_blocked",input.mouse_states_blocked},
                    {"keyboard_states_blocked",input.keyboard_states_blocked},{"buffered_reads_blocked",input.buffered_reads_blocked},
                    {"cursor_warps_blocked",input.cursor_warps_blocked},{"cursor_clips_blocked",input.cursor_clips_blocked},
                    {"key_polls_blocked",input.key_polls_blocked},{"devices_suspended",input.devices_suspended},{"devices_resumed",input.devices_resumed}};
                write_json(session/L"diagnostics.json",diagnostic);last_diagnostic=GetTickCount64();
            }
            if(gameplay_serial.load()!=last_serial){last_serial=gameplay_serial.load();write_json(session/L"event.json",{{"serial",last_serial}});}
            std::error_code ec;auto stamp=fs::last_write_time(session/L"state.json",ec);
            if(!ec&&stamp!=last_stamp) {
                json next=read_json(session/L"state.json");
                const auto request=read_json(session/L"control.json");
                const bool matches=next.contains("request")&&string_value(next["request"],"account")==string_value(request,"account")&&string_value(next["request"],"profile")==string_value(request,"profile");
                if(matches&&next.contains("achievements")&&next["achievements"].is_array()&&next.contains("accounts")&&next["accounts"].is_array()) {
                    std::lock_guard lock(data_mutex);model=std::move(next);last_stamp=stamp;runtime_error.clear();
                }
            }
            update_map_selection();
            const auto error=read_json(session/L"error.json");
            if(error.contains("error")){std::lock_guard lock(data_mutex);runtime_error=string_value(error,"error")+". Показан последний успешный результат.";}
            if(WaitForSingleObject(child,0)==WAIT_OBJECT_0) {
                std::lock_guard lock(data_mutex);runtime_error="Модуль чтения остановился. Данные на панели могут быть устаревшими.";break;
            }
        }catch(const std::exception&) {std::lock_guard lock(data_mutex);runtime_error="Ошибка чтения данных. Последний успешный результат сохранён.";}
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
}
bool StartAssistant() {
    if(reader.joinable())return false;
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&StartAssistant),&module);
    wchar_t module_path[32768]{};GetModuleFileNameW(module,module_path,32768);plugin_dir=fs::path(module_path).parent_path();
    PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return false;
    auto root=fs::path(local)/aa::game::RuntimeFolder;CoTaskMemFree(local);
    wchar_t override_root[32768]{};
    DWORD count=GetEnvironmentVariableW(L"AA_ASSISTANT_DATA_DIR",override_root,32768);
    if(count&&count<32768&&fs::path(override_root).is_absolute())root=override_root;
    session=root/L"sessions"/std::to_wstring(GetCurrentProcessId());settings_path=root/L"preferences.json";
    std::error_code ec;fs::create_directories(session,ec);if(ec)return false;
    {std::lock_guard lock(event_mutex);telemetry_run=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());telemetry_queue.clear();telemetry_configuration=json::object();telemetry_sequence=telemetry_written=telemetry_dropped=0;telemetry_error.clear();}
    fs::create_directories(root/L"telemetry",ec);if(ec)return false;
    telemetry_journal=root/L"telemetry"/(telemetry_run+".jsonl");
    preferences=read_json(settings_path);if(!preferences.contains("plan")||!preferences["plan"].is_object())preferences["plan"]=json::object();
    model=json::object();runtime_error.clear();visible=preferences.contains("open_on_start")&&preferences["open_on_start"]==true;
    selected_id=string_value(preferences,"selected_id");filter=std::clamp(preferences.value("achievement_view",0),0,4);stopping=false;toggle_held=false;toggle_keyup_pending=false;renderer_ready=false;panel_active=false;presented_frames=0;drawn_frames=0;save_control();
    // A new session must not display stale data left by a reused Windows PID.
    fs::remove(session/L"state.json",ec);fs::remove(session/L"error.json",ec);
    if(!aa::input::Install())return false;
    aa::mapprobe::Install(plugin_dir,session);
    if(!install_hook()){aa::mapprobe::Stop();aa::input::Stop();return false;}
    reader=std::thread(read_loop);return true;
}
void StopAssistant() {
    if(!reader.joinable())return;
    stopping=true;
    aa::mapprobe::Stop();
    if(present_target)MH_DisableHook(present_target);
    while(active_presents.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {std::lock_guard lock(ui_mutex);visible=false;panel_active=false;}
    aa::input::Stop();
    {
        std::lock_guard lock(ui_mutex);visible=false;
        if(ready) {
            if(game_window&&original_wndproc&&reinterpret_cast<WNDPROC>(GetWindowLongPtrW(game_window,GWLP_WNDPROC))==window_proc)
                SetWindowLongPtrW(game_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original_wndproc));
            ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();
        }
        ready=false;renderer_ready=false;active_swap=nullptr;context.Reset();device.Reset();save_control(true);
    }
    reader.join();
    flush_telemetry();
    if(child){WaitForSingleObject(child,2000);CloseHandle(child);child=nullptr;}
    if(present_target)MH_RemoveHook(present_target);
    present_target=nullptr;original_present=nullptr;game_window=nullptr;original_wndproc=nullptr;
}
void AssistantPaused(bool value){paused=value;}
void AssistantGameplay(const char* name,const char* payload){capture_telemetry(name,payload,false);if(name){std::lock_guard lock(event_mutex);last_event=std::string(name).substr(0,200);++gameplay_serial;}}

void AssistantConfiguration(const char* name,const char* payload){capture_telemetry(name,payload,true);}
