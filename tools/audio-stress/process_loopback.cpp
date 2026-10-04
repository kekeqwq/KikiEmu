// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only process loopback, not microphone or whole-desktop capture.
// ABI/usage reference: Microsoft's ApplicationLoopback sample and Windows SDK
// audioclientactivationparams.h (Windows build 20348+). Never changes volumes,
// default endpoints, drivers, or any global host audio settings.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>
#include "../../tests/system_fixture.hpp"

using Microsoft::WRL::ComPtr;
using namespace kiki;
// MSYS2's headers do not yet expose this public Windows SDK ABI.
struct ProcessLoopbackParams { DWORD target; int mode; };
struct ActivationParams { int type; union { ProcessLoopbackParams process; }; };
static void checked(HRESULT hr, const char* where) {
    if (FAILED(hr)) { std::ostringstream s; s << where << " HRESULT=0x" << std::hex << uint32_t(hr); throw std::runtime_error(s.str()); }
}
class NewFile {
    HANDLE handle=INVALID_HANDLE_VALUE;
public:
    explicit NewFile(const fs::path& path) {
        handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot exclusively create new output file");
    }
    ~NewFile(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    void write(const void* p,DWORD size) {DWORD n;if(!WriteFile(handle,p,size,&n,nullptr)||n!=size)throw std::runtime_error("Capture output write failed");}
    void rewind(){LARGE_INTEGER zero{};if(!SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot update WAV header");}
    void flush(){if(!FlushFileBuffers(handle))throw std::runtime_error("Cannot flush capture output");}
};
class Completion final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
    std::atomic<ULONG> refs{1};
public:
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioClient> client;
    HRESULT result = E_PENDING;
    ~Completion() { CloseHandle(event); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** p) override {
        if (!p) return E_POINTER;
        *p = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IActivateAudioInterfaceCompletionHandler)) *p = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (id == __uuidof(IAgileObject)) *p = static_cast<IAgileObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        ComPtr<IUnknown> object;
        HRESULT activation;
        result=op->GetActivateResult(&activation,object.GetAddressOf());
        if(SUCCEEDED(result)) result=activation;
        if(SUCCEEDED(result)) result=object.As(&client);
        SetEvent(event); return S_OK;
    }
};
static void inspect_volumes(DWORD pid) {
    // Read-only: enumerate sessions, never call SetMute/SetMasterVolume.
    ComPtr<IMMDeviceEnumerator> devices;
    checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                             __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(devices.GetAddressOf())),"Devices");
    ComPtr<IMMDeviceCollection> endpoints; checked(devices->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,endpoints.GetAddressOf()),"Endpoints");
    UINT count; checked(endpoints->GetCount(&count),"EndpointCount");
    for(UINT i=0;i<count;++i) {
        ComPtr<IMMDevice> device;checked(endpoints->Item(i,device.GetAddressOf()),"Endpoint");
        ComPtr<IAudioSessionManager2> manager;
        if(FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf())))) continue;
        ComPtr<IAudioSessionEnumerator> sessions;checked(manager->GetSessionEnumerator(sessions.GetAddressOf()),"Sessions");
        int n;checked(sessions->GetCount(&n),"SessionCount");
        for(int j=0;j<n;++j) {
            ComPtr<IAudioSessionControl> session;checked(sessions->GetSession(j,session.GetAddressOf()),"Session");
            ComPtr<IAudioSessionControl2> control;if(FAILED(session.As(&control)))continue;
            DWORD owner;if(FAILED(control->GetProcessId(&owner)) || owner!=pid)continue;
            ComPtr<ISimpleAudioVolume> volume;checked(session.As(&volume),"SessionVolume");
            float gain;BOOL mute;AudioSessionState state;
            checked(volume->GetMasterVolume(&gain),"GetVolume");checked(volume->GetMute(&mute),"GetMute");checked(session->GetState(&state),"GetState");
            std::cout<<"OWNED_AUDIO_SESSION endpoint="<<i<<" state="<<state<<" gain="<<gain<<" mute="<<mute<<std::endl;
        }
    }
}
static nlohmann::json read_marker(const fs::path& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Missing developer fixture marker");
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    if(!GetFileInformationByHandle(file,&info) || info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>2*1024*1024) {CloseHandle(file);throw std::runtime_error("Invalid marker");}
    std::string bytes(size_t(size.QuadPart),'\0'); DWORD count;
    bool ok=ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr) && count==bytes.size(); CloseHandle(file);
    if(!ok) throw std::runtime_error("Truncated fixture marker");
    return parse_json_document(bytes);
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=5) throw std::runtime_error("Usage: process-loopback FIXTURE_ROOT RUNNER_APP_ROOT SECONDS NEW_OUTPUT_WAV");
        auto root=normalize_directory(argv[1]), app=normalize_directory(argv[2]);
        auto f=kiki_test::validate_system_fixture(read_marker(root/L"system-regression.json"),root,app);
        StorageLease rootLease(f.root,{f.manager.appRoot}), instanceLease(f.instance,{f.manager.appRoot,f.manager.registryRoot});
        OwnedProcess target;
        {
            RegistryTransaction registry(f.manager.registryRoot,"release");
            if(registry.state().at("instances").size()!=1 || !registry.state().at("defaultId").is_null()) throw std::runtime_error("Public/multiple/default registry refused");
            const auto& r=registry.instance(f.id); kiki_test::verify_system_fixture_registration(f,r);
            if(r.at("lifecycle")!="running") throw std::runtime_error("Wait for the owned developer guest to be ready");
            unsigned matches=0;
            for(const auto& p:r.at("runtime").at("processes")) if(p.at("role")=="qemu") {target=parse_owned_process(p);++matches;}
            if(matches!=1 || !owned_process_is_live(target)) throw std::runtime_error("No unique owned live QEMU");
        }
        HANDLE pin=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,target.pid);
        if(!pin) throw std::runtime_error("Cannot pin process");
        FILETIME created,exited,kernel,user;
        if(!GetProcessTimes(pin,&created,&exited,&kernel,&user) || (uint64_t(created.dwHighDateTime)<<32|created.dwLowDateTime)!=target.creationTime || !owned_process_is_live(target)) {CloseHandle(pin);throw std::runtime_error("PID changed");}
        int seconds=std::stoi(argv[3]); if(seconds<1||seconds>1800)throw std::runtime_error("Use 1..1800 seconds");
        fs::path output(argv[4]);
        if(fs::exists(output) || fs::exists(output.wstring()+L".csv")) throw std::runtime_error("Output must be NEW");
        checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");
        inspect_volumes(target.pid);
        HMODULE mm=LoadLibraryW(L"Mmdevapi.dll");
        using Activate=HRESULT (WINAPI*)(LPCWSTR,REFIID,PROPVARIANT*,IActivateAudioInterfaceCompletionHandler*,IActivateAudioInterfaceAsyncOperation**);
        auto activate=mm?reinterpret_cast<Activate>(GetProcAddress(mm,"ActivateAudioInterfaceAsync")):nullptr;
        if(!activate) throw std::runtime_error("Process loopback unsupported");
        ComPtr<Completion> completion; completion.Attach(new Completion);
        ActivationParams parameters{};parameters.type=1;parameters.process.target=target.pid;parameters.process.mode=0;
        PROPVARIANT prop{};prop.vt=VT_BLOB;prop.blob.cbSize=sizeof(parameters);prop.blob.pBlobData=reinterpret_cast<BYTE*>(&parameters);
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        checked(activate(L"VAD\\Process_Loopback",__uuidof(IAudioClient),&prop,completion.Get(),operation.GetAddressOf()),"Activate");
        if(WaitForSingleObject(completion->event,10000)!=WAIT_OBJECT_0) throw std::runtime_error("Activation timeout");
        checked(completion->result,"Activated");
        auto client=completion->client;
        WAVEFORMATEX format{WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
        checked(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,0,0,&format,nullptr),"Initialize");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); checked(client->SetEventHandle(event),"SetEvent");
        ComPtr<IAudioCaptureClient> capture; checked(client->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(capture.GetAddressOf())),"CaptureClient");
        NewFile wav(output),csv(fs::path(output.wstring()+L".csv"));
        uint8_t header[44]{}; wav.write(header,44);
        std::string csvHeader="host_tick_ms,device_frames,qpc_100ns,frames,flags,rms,peak\n";
        csv.write(csvHeader.data(),DWORD(csvHeader.size()));
        checked(client->Start(),"Start");auto start=GetTickCount64();uint64_t samples=0;
        std::cout<<"PROCESS_LOOPBACK_STARTED pid="<<target.pid<<" tick="<<start<<" seconds="<<seconds<<std::endl;
        while(GetTickCount64()-start<uint64_t(seconds)*1000 && WaitForSingleObject(pin,0)==WAIT_TIMEOUT) {
            WaitForSingleObject(event,1000); UINT32 packet;
            checked(capture->GetNextPacketSize(&packet),"NextPacket");
            while(packet) {
                BYTE* data;UINT32 frames;DWORD flags;UINT64 pos,qpc;
                checked(capture->GetBuffer(&data,&frames,&flags,&pos,&qpc),"GetBuffer");
                std::vector<int16_t> zeros;
                if(flags&AUDCLNT_BUFFERFLAGS_SILENT){zeros.assign(size_t(frames)*2,0);data=reinterpret_cast<BYTE*>(zeros.data());}
                auto values=reinterpret_cast<int16_t*>(data);double sum=0,peak=0;
                for(size_t i=0;i<size_t(frames)*2;++i){double v=values[i]/32768.;sum+=v*v;peak=std::max(peak,std::abs(v));}
                wav.write(data,frames*4);samples+=frames;
                std::ostringstream row;row<<GetTickCount64()<<','<<pos<<','<<qpc<<','<<frames<<','<<flags<<','<<std::sqrt(sum/(frames*2))<<','<<peak<<'\n';
                auto line=row.str();csv.write(line.data(),DWORD(line.size()));
                checked(capture->ReleaseBuffer(frames),"ReleaseBuffer");checked(capture->GetNextPacketSize(&packet),"NextPacket");
            }
        }
        checked(client->Stop(),"Stop");
        inspect_volumes(target.pid);
        uint32_t bytes=uint32_t(samples*4);std::memcpy(header,"RIFF",4);uint32_t length=36+bytes;std::memcpy(header+4,&length,4);std::memcpy(header+8,"WAVEfmt ",8);length=16;std::memcpy(header+16,&length,4);std::memcpy(header+20,&format,16);std::memcpy(header+36,"data",4);std::memcpy(header+40,&bytes,4);
        wav.rewind();wav.write(header,44);wav.flush();csv.flush();CloseHandle(event);CloseHandle(pin);
        std::cout<<"PROCESS_LOOPBACK_COMPLETED frames="<<samples<<" bytes="<<bytes<<std::endl;
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Owned process-loopback error: "<<e.what()<<std::endl;return 1;}
}
