// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-fixture-only file staging over the existing owned private shell.
// No adb.exe/server, public registry, HTTP listener or shared folder.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include "../../tests/system_fixture.hpp"
#include "../../src/kikiemu/transport.hpp"
using namespace kiki;
static nlohmann::json marker(const fs::path& path) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Missing fixture");
    BY_HANDLE_FILE_INFORMATION i{};LARGE_INTEGER size{};
    if(!GetFileInformationByHandle(h,&i) || i.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(h,&size) || size.QuadPart<=0 || size.QuadPart>2097152){CloseHandle(h);throw std::runtime_error("Invalid fixture");}
    std::string text(size_t(size.QuadPart),'\0');DWORD n;
    bool ok=ReadFile(h,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size();CloseHandle(h);
    if(!ok)throw std::runtime_error("Truncated marker");return parse_json_document(text);
}
static std::string base64(const std::string& input) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;unsigned accumulator=0;int bits=0;
    for(unsigned char c:input){accumulator=(accumulator<<8)|c;bits+=8;while(bits>=6){bits-=6;out+=alphabet[(accumulator>>bits)&63];}}
    if(bits)out+=alphabet[(accumulator<<(6-bits))&63];while(out.size()%4)out+='=';return out;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: guest-stage FIXTURE_ROOT RUNNER_APP_ROOT CANDIDATE_FILE");
        auto root=normalize_directory(argv[1]),app=normalize_directory(argv[2]);
        auto f=kiki_test::validate_system_fixture(marker(root/L"system-regression.json"),root,app);
        StorageLease rootLease(f.root,{f.manager.appRoot}),instanceLease(f.instance,{f.manager.appRoot,f.manager.registryRoot});
        OwnedProcess target;uint16_t port=0;
        { RegistryTransaction registry(f.manager.registryRoot,"release");
          if(registry.state().at("instances").size()!=1 || !registry.state().at("defaultId").is_null())throw std::runtime_error("Refusing public registry");
          const auto& record=registry.instance(f.id);kiki_test::verify_system_fixture_registration(f,record);
          if(record.at("lifecycle")!="running")throw std::runtime_error("Guest not ready");
          unsigned matches=0;
          for(const auto& p:record.at("runtime").at("processes"))if(p.at("role")=="qemu"){target=parse_owned_process(p);matches++;}
          if(matches!=1 || !owned_process_is_live(target))throw std::runtime_error("No unique owned QEMU");
          matches=0;
          for(const auto& e:record.at("runtime").at("endpoints"))if(e.at("role")=="adb"){if(e.at("pid")!=target.pid)throw std::runtime_error("Wrong endpoint owner");port=e.at("port");matches++;}
          if(matches!=1 || !port)throw std::runtime_error("Invalid private endpoint");
        }
        // Pin/read the exact candidate once; no stream of a mutable pathname.
        fs::path source=fs::absolute(argv[3]);HANDLE file=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Candidate not found");
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(file,&info) || info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>32*1024*1024){CloseHandle(file);throw std::runtime_error("Invalid candidate");}
        std::string bytes(size_t(size.QuadPart),'\0');DWORD n;
        bool ok=ReadFile(file,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size();
        if(!ok){CloseHandle(file);throw std::runtime_error("Truncated candidate");}
        auto sha=sha256_text(bytes);CloseHandle(file);
        const std::string dest="/data/local/tmp/kiki-audio03/audio-service-candidate";
        auto shell=[&](const std::string& command){auto r=guest_shell(target,port,command,30000);if(r.exitCode)throw std::runtime_error("Staging failed: "+r.error);return r.output;};
        shell("mkdir -p /data/local/tmp/kiki-audio03; test ! -e "+dest+" && : > "+dest+".b64");
        auto encoded=base64(bytes);
        for(size_t i=0;i<encoded.size();i+=30000){shell("printf '%s' '"+encoded.substr(i,30000)+"' >> "+dest+".b64");if(i%300000==0)std::cout<<"STAGED_BASE64_BYTES="<<i<<std::endl;}
        auto remote=shell("base64 -d "+dest+".b64 > "+dest+" && sha256sum "+dest);
        if(!remote.starts_with(sha+" "))throw std::runtime_error("Guest byte hash differs");
        std::cout<<"CANDIDATE_STAGED sha256="<<sha<<" bytes="<<bytes.size()<<" pid="<<target.pid<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"Developer guest-stage error: "<<e.what()<<std::endl;return 1;}
}
