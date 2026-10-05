// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-fixture-only QMP mixer WAV capture, never a user's QEMU monitor.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <fstream>
#include <iostream>
#include "../../tests/system_fixture.hpp"
using namespace kiki;
using json=nlohmann::json;
struct Handle { HANDLE h=INVALID_HANDLE_VALUE; ~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
static json read_marker(const fs::path& path) {
    Handle h{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    BY_HANDLE_FILE_INFORMATION i{}; LARGE_INTEGER size{};
    if(h.h==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h.h,&i) ||
       i.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY) ||
       !GetFileSizeEx(h.h,&size) || size.QuadPart<=0 || size.QuadPart>2097152)
        throw std::runtime_error("Invalid fixture marker");
    std::string bytes(size_t(size.QuadPart),'\0'); DWORD n=0;
    if(!ReadFile(h.h,bytes.data(),DWORD(bytes.size()),&n,nullptr) || n!=bytes.size())
        throw std::runtime_error("Truncated fixture marker");
    return parse_json_document(bytes);
}
class Qmp {
    SOCKET s=INVALID_SOCKET; bool initialized=false; OwnedProcess target; uint16_t port; unsigned seq=0;
    void verify() { if(!owned_process_is_live(target))throw std::runtime_error("Owned QEMU changed"); verify_owned_endpoints({target},{{target.pid,port}}); }
    json line(uint64_t deadline) {
        std::string text;
        while(text.size()<65536 && GetTickCount64()<deadline) {
            const auto now=GetTickCount64(); if(now>=deadline)break;
            DWORD timeout=DWORD(deadline-now);
            if(setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout)))
                throw std::runtime_error("QMP receive timeout failed");
            char c; if(recv(s,&c,1,0)!=1)throw std::runtime_error("Bounded QMP read failed");
            if(c=='\n')return parse_json_document(text); text+=c;
        }
        throw std::runtime_error("QMP reply too large or late");
    }
    json command(const json& request) {
        verify(); auto id="audio-"+std::to_string(++seq); auto body=request;body["id"]=id;
        auto bytes=body.dump()+"\r\n";size_t written=0;
        const uint64_t deadline=GetTickCount64()+5000;
        while(written<bytes.size()) { int n=send(s,bytes.data()+written,int(bytes.size()-written),0);if(n<=0)throw std::runtime_error("QMP send failed");written+=n; }
        for(unsigned i=0;i<64;++i) {auto r=line(deadline);if(r.contains("event"))continue;
            if(r.value("id",std::string())!=id || r.contains("error") || !r.contains("return"))throw std::runtime_error("Unexpected QMP response: "+r.dump());
            return r.at("return");}
        throw std::runtime_error("Too many QMP events");
    }
public:
    Qmp(const OwnedProcess& p,uint16_t q):target(p),port(q) {
        try {
            WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock failed");initialized=true;
            verify();s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)throw std::runtime_error("QMP socket failed");
            u_long nonblock=1;if(ioctlsocket(s,FIONBIO,&nonblock))throw std::runtime_error("QMP nonblocking failed");
            sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);
            if(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))==SOCKET_ERROR) {
                if(WSAGetLastError()!=WSAEWOULDBLOCK)throw std::runtime_error("QMP connect failed");
                fd_set w,e;FD_ZERO(&w);FD_ZERO(&e);FD_SET(s,&w);FD_SET(s,&e);timeval t{5,0};
                if(select(0,nullptr,&w,&e,&t)<=0 || FD_ISSET(s,&e))throw std::runtime_error("QMP connect timed out");
                int error=0,len=sizeof(error);if(getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&len) || error)throw std::runtime_error("QMP connect error");
            }
            nonblock=0;if(ioctlsocket(s,FIONBIO,&nonblock))throw std::runtime_error("QMP blocking failed");
            DWORD ms=5000;if(setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&ms),sizeof(ms)))throw std::runtime_error("QMP timeout failed");
            verify();if(!line(GetTickCount64()+5000).contains("QMP"))throw std::runtime_error("Endpoint is not QMP");
            command(json{{"execute","qmp_capabilities"}});
        } catch(...) {if(s!=INVALID_SOCKET)closesocket(s);if(initialized)WSACleanup();throw;}
    }
    ~Qmp(){if(s!=INVALID_SOCKET)closesocket(s);if(initialized)WSACleanup();}
    std::string hmp(const std::string& text) {return command(json{{"execute","human-monitor-command"},{"arguments",{{"command-line",text}}}}).get<std::string>();}
};
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: qemu-capture FIXTURE_ROOT RUNNER_APP_ROOT SECONDS");
        std::wstring duration=argv[3];if(duration.empty() || duration.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Invalid seconds");
        unsigned seconds=std::stoul(duration);if(!seconds || seconds>1800)throw std::runtime_error("Invalid duration");
        auto root=normalize_directory(argv[1]),app=normalize_directory(argv[2]);
        auto f=kiki_test::validate_system_fixture(read_marker(root/L"system-regression.json"),root,app);
        StorageLease rootLease(f.root,{f.manager.appRoot}),instanceLease(f.instance,{f.manager.appRoot,f.manager.registryRoot});
        OwnedProcess target;uint16_t port=0;
        {RegistryTransaction r(f.manager.registryRoot,"release");
         if(r.state().at("instances").size()!=1 || !r.state().at("defaultId").is_null())throw std::runtime_error("Refusing public registry");
         auto record=r.instance(f.id);kiki_test::verify_system_fixture_registration(f,record);
         if(record.at("lifecycle")!="running")throw std::runtime_error("Guest is not ready");
         unsigned count=0;for(const auto& p:record.at("runtime").at("processes"))if(p.at("role")=="qemu"){target=parse_owned_process(p);count++;}
         if(count!=1 || !owned_process_is_live(target))throw std::runtime_error("No unique owned QEMU");
         count=0;for(const auto& e:record.at("runtime").at("endpoints"))if(e.at("role")=="qmp"){if(e.at("pid")!=target.pid)throw std::runtime_error("Wrong QMP owner");port=e.at("port");count++;}
         if(count!=1 || !port)throw std::runtime_error("Invalid owned QMP");}
        Handle process{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,target.pid)};
        if(!process.h || process.h==INVALID_HANDLE_VALUE || !owned_process_is_live(target))throw std::runtime_error("Cannot pin QEMU");
        Qmp qmp(target,port);
        if(!qmp.hmp("info capture").empty())throw std::runtime_error("Existing capture; refusing to interfere");
        auto directory=root/fs::path("audio-capture-"+new_instance_uuid());
        auto storage=create_storage(directory,new_instance_uuid(),"release",{f.manager.appRoot,f.manager.registryRoot});
        StorageLease outputLease(storage,{f.manager.appRoot,f.manager.registryRoot});
        auto file=directory/L"qemu.wav";auto path=utf8(file.generic_wstring());
        if(path.find_first_of("\"\r\n")!=std::string::npos || fs::exists(file))throw std::runtime_error("Invalid new WAV path");
        std::ofstream(directory/L"ownership.json")<<json{{"qemu",owned_process_json(target)},{"qmpPort",port},{"wav",path},{"seconds",seconds}}.dump(2);
        bool active=true; // A reply can fail after QEMU starts; still attempt owned cleanup.
        try {
            auto result=qmp.hmp("wavcapture \""+path+"\" kiki_audio 48000 16 2");
            auto info=qmp.hmp("info capture");
            if(info.find(path)==std::string::npos || !fs::exists(file))throw std::runtime_error("WAV capture did not start: "+result+info);
            std::cout<<"QEMU_BACKEND_CAPTURE_STARTED "<<json{{"pid",target.pid},{"wav",path},{"tick",GetTickCount64()}}.dump()<<std::endl;
            if(WaitForSingleObject(process.h,seconds*1000)!=WAIT_TIMEOUT)throw std::runtime_error("QEMU exited during capture");
            if(qmp.hmp("info capture").find(path)==std::string::npos)throw std::runtime_error("Capture ownership changed");
            qmp.hmp("stopcapture 0");active=false;
            if(!qmp.hmp("info capture").empty())throw std::runtime_error("Capture did not stop");
            std::cout<<"QEMU_BACKEND_CAPTURE_COMPLETED "<<path<<std::endl;
        }catch(...){if(active)try{if(qmp.hmp("info capture").find(path)!=std::string::npos)qmp.hmp("stopcapture 0");}catch(...){}throw;}
        return 0;
    }catch(const std::exception& e){std::cerr<<"Owned QEMU capture error: "<<e.what()<<std::endl;return 1;}
}
