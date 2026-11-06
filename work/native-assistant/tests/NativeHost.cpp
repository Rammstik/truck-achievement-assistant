#include "../src/GameConfig.h"
// Graphics/SDK harness. Never starts or attaches to ETS2.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <map>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include "json.hpp"
#include "scssdk_telemetry.h"
using Microsoft::WRL::ComPtr;
struct Callback {scs_telemetry_event_callback_t fn;scs_context_t context;};
static std::map<scs_event_t,Callback> callbacks;
static SCSAPI_RESULT register_event(const scs_event_t type,const scs_telemetry_event_callback_t fn,const scs_context_t context){callbacks[type]={fn,context};return SCS_RESULT_ok;}
static SCSAPI_RESULT unregister_event(const scs_event_t type){callbacks.erase(type);return SCS_RESULT_ok;}
static SCSAPI_VOID log_message(const scs_log_type_t,const scs_string_t message){OutputDebugStringA(message);}
static bool quit=false;
// Share delete access so the worker can atomically replace snapshots while this
// harness polls them. Parse only after closing the file, as the plugin does.
static nlohmann::json read_snapshot(const std::filesystem::path& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return nlohmann::json();
    LARGE_INTEGER size{};std::string content;
    if(GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<32*1024*1024) {
        content.resize(static_cast<size_t>(size.QuadPart));DWORD read=0;
        if(!ReadFile(file,content.data(),static_cast<DWORD>(content.size()),&read,nullptr)||read!=content.size())content.clear();
    }
    CloseHandle(file);return nlohmann::json::parse(content,nullptr,false);
}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM p,LPARAM l){
    if(m==WM_CLOSE){quit=true;return 0;}
    return DefWindowProcW(w,m,p,l);
}
// Export this test application's own preserved DirectX backbuffer.
static bool capture_frame(ID3D11Device* device,ID3D11DeviceContext* context,IDXGISwapChain* swap,const std::filesystem::path& path){
    ComPtr<ID3D11Texture2D> source,staging;if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&source))))return false;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return false;context->CopyResource(staging.Get(),source.Get());
    D3D11_MAPPED_SUBRESOURCE pixels{};if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels)))return false;
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof file+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+d.Width*d.Height*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof info;info.biWidth=d.Width;info.biHeight=-LONG(d.Height);info.biPlanes=1;info.biBitCount=32;info.biSizeImage=d.Width*d.Height*4;
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(&file),sizeof file);output.write(reinterpret_cast<const char*>(&info),sizeof info);
    std::vector<unsigned char> row(d.Width*4);
    for(unsigned y=0;y<d.Height;++y){memcpy(row.data(),static_cast<const unsigned char*>(pixels.pData)+y*pixels.RowPitch,row.size());for(unsigned x=0;x<d.Width;++x)std::swap(row[x*4],row[x*4+2]);output.write(reinterpret_cast<const char*>(row.data()),row.size());}
    context->Unmap(staging.Get(),0);return bool(output);
}
int wmain(int argc,wchar_t** argv){
    wchar_t own_path[32768]{};GetModuleFileNameW(nullptr,own_path,32768);
    auto directory=std::filesystem::path(own_path).parent_path();
    auto plugin_path=argc>1?std::filesystem::path(argv[1]):directory/L"AchievementAssistant.dll";
    wchar_t data_root[32768]{};
    if(!GetEnvironmentVariableW(L"AA_ASSISTANT_DATA_DIR",data_root,32768))
        SetEnvironmentVariableW(L"AA_ASSISTANT_DATA_DIR",(directory/L"host-test-data").c_str());
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"AA_Native_Test";RegisterClassW(&cls);
    const bool compact=argc>4&&std::wstring(argv[4])==L"--compact";
    const bool offscreen=argc>2&&std::wstring(argv[2])==L"--capture";
    HWND window=CreateWindowW(cls.lpszClassName,L"Achievement Assistant · тест DX11 (не ETS2)",WS_OVERLAPPEDWINDOW,offscreen?-30000:100,offscreen?-30000:70,compact?1100:1280,compact?740:850,nullptr,nullptr,cls.hInstance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.SampleDesc.Count=1;desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_SEQUENTIAL;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> swap;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
    if(FAILED(hr))hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
    if(FAILED(hr))return 3;
    HMODULE plugin=LoadLibraryW(plugin_path.c_str());if(!plugin)return 4;
    const auto init=reinterpret_cast<decltype(&scs_telemetry_init)>(GetProcAddress(plugin,"scs_telemetry_init"));
    const auto shutdown=reinterpret_cast<decltype(&scs_telemetry_shutdown)>(GetProcAddress(plugin,"scs_telemetry_shutdown"));
    if(!init||!shutdown)return 5;
    scs_telemetry_init_params_v101_t params{};params.common.game_id=aa::game::SdkId;params.common.game_name="Native graphics test";params.common.game_version=SCS_MAKE_VERSION(1,18);params.common.log=log_message;
    params.register_for_event=register_event;params.unregister_from_event=unregister_event;
    if(init(SCS_MAKE_VERSION(99,99),&params)!=SCS_RESULT_unsupported)return 6;
    params.common.game_id=aa::game::IsATS?"eut2":"ats";
    if(init(SCS_TELEMETRY_VERSION_1_01,&params)!=SCS_RESULT_unsupported)return 15;
    params.common.game_id=aa::game::SdkId;
    if(init(SCS_TELEMETRY_VERSION_1_01,&params)!=SCS_RESULT_ok)return 7;
    // Exercise payload copying through the real DLL callback before frame rendering.
    scs_named_value_t attrs[4]{};
    attrs[0].name="revenue";attrs[0].index=SCS_U32_NIL;attrs[0].value.type=SCS_VALUE_TYPE_s64;attrs[0].value.value_s64.value=9007199254740993LL;
    attrs[1].name="cargo.damage";attrs[1].index=SCS_U32_NIL;attrs[1].value.type=SCS_VALUE_TYPE_float;attrs[1].value.value_float.value=0.0f;
    attrs[2].name="source.city";attrs[2].index=SCS_U32_NIL;attrs[2].value.type=SCS_VALUE_TYPE_string;attrs[2].value.value_string.value="test_city";
    scs_telemetry_configuration_t config{};config.id="job";config.attributes=attrs;
    auto c=callbacks.at(SCS_TELEMETRY_EVENT_configuration);c.fn(SCS_TELEMETRY_EVENT_configuration,&config,c.context);
    scs_telemetry_gameplay_event_t delivered{};delivered.id="job.delivered";delivered.attributes=attrs;
    c=callbacks.at(SCS_TELEMETRY_EVENT_gameplay);c.fn(SCS_TELEMETRY_EVENT_gameplay,&delivered,c.context);
    delivered.id="job.cancelled";c.fn(SCS_TELEMETRY_EVENT_gameplay,&delivered,c.context);
    attrs[0].value.value_s64.value=0;attrs[2].value.value_string.value="mutated_after_callback";
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    ULONGLONG start=GetTickCount64();unsigned frames=0;bool resized=false;int phase=0;
    uint64_t previous_drawn=0;
    const bool timed=argc>2&&std::wstring(argv[2])==L"--timed";
    const bool capture=argc>3&&std::wstring(argv[2])==L"--capture";bool captured=false;unsigned readyFrame=0;
    while(!quit&&(!(timed||capture)||GetTickCount64()-start<20000)){
        MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> target;
        if(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)))&&SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target))){
            const FLOAT color[4]={.03f,.05f,.07f,1};context->ClearRenderTargetView(target.Get(),color);
        }
        target.Reset();buffer.Reset();
        if(FAILED(swap->Present(1,0)))return 8;++frames;
        if(capture&&frames>90){
            const auto statePath=std::filesystem::path(data_root)/L"sessions"/std::to_wstring(GetCurrentProcessId())/L"state.json";
            const auto snapshot=read_snapshot(statePath);
            if(snapshot.is_object()&&snapshot.contains("details")){
                if(!readyFrame)readyFrame=frames;
                if(frames<readyFrame+120)continue;
                captured=capture_frame(device.Get(),context.Get(),swap.Get(),argv[3]);
                const auto diagnosticPath=std::filesystem::path(data_root)/L"sessions"/std::to_wstring(GetCurrentProcessId())/L"diagnostics.json";
                {const auto diagnostic=read_snapshot(diagnosticPath);if(diagnostic.is_object()){auto sidecar=std::filesystem::path(argv[3]);sidecar.replace_extension(L".json");std::ofstream output(sidecar);output<<diagnostic.dump();}}
                quit=true;
            }
        }
        if(timed&&!resized&&frames>15){
            context->OMSetRenderTargets(0,nullptr,nullptr);context->Flush();
            if(FAILED(swap->ResizeBuffers(0,1100,740,DXGI_FORMAT_UNKNOWN,0)))return 9;
            resized=true;
        }
        if(timed){
            auto diagnostics=std::filesystem::path(data_root)/L"sessions"/std::to_wstring(GetCurrentProcessId())/L"diagnostics.json";
            const auto info=read_snapshot(diagnostics);
            if(info.is_object()&&info.value("renderer_ready",false)&&info.value("drawn_frames",uint64_t(0))>previous_drawn+5){
                bool shown=info.value("panel_visible",false),is_paused=info.value("paused",true);
                bool captured=info.contains("input")&&info["input"].value("captured",false);
                if(phase==0&&shown&&captured&&is_paused){
                    std::ofstream file(std::filesystem::path(data_root)/L"open-render-check.json");file<<info.dump();
                    auto callback=callbacks.at(SCS_TELEMETRY_EVENT_started);callback.fn(SCS_TELEMETRY_EVENT_started,nullptr,callback.context);++phase;previous_drawn=info["drawn_frames"];
                }else if(phase==1&&shown&&captured&&!is_paused){
                    std::ofstream file(std::filesystem::path(data_root)/L"driving-render-check.json");file<<info.dump();
                    auto callback=callbacks.at(SCS_TELEMETRY_EVENT_paused);callback.fn(SCS_TELEMETRY_EVENT_paused,nullptr,callback.context);++phase;previous_drawn=info["drawn_frames"];
                }else if(phase==2&&shown&&captured&&is_paused){
                    // Internal focus-loss notification, no desktop input injection.
                    SendMessageW(window,WM_KILLFOCUS,0,0);++phase;previous_drawn=info["drawn_frames"];
                }else if(phase==3&&!shown&&!captured&&is_paused){
                    std::ofstream file(std::filesystem::path(data_root)/L"render-check.json");file<<info.dump();++phase;quit=true;
                }
            }
        }
        Sleep(10);
    }
    const int verificationFailure=timed&&phase!=4?13:capture&&!captured?14:0;
    shutdown();callbacks.clear();
    // Repeat the SDK lifecycle in the same loaded DLL. Shutdown must remove
    // hooks/subclass and finish the child before the module is unloaded.
    if(init(SCS_TELEMETRY_VERSION_1_01,&params)!=SCS_RESULT_ok)return 10;
    swap->Present(0,0);shutdown();callbacks.clear();FreeLibrary(plugin);
    if(FAILED(swap->Present(0,0)))return 11;
    DestroyWindow(window);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    return verificationFailure?verificationFailure:frames>0?0:12;
}
