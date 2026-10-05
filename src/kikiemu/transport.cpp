// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include "transport.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <fstream>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
struct Winsock {
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("Could not initialize private socket transport."); }
    ~Winsock() { WSACleanup(); }
};
struct Socket { SOCKET value = INVALID_SOCKET; ~Socket() { if (value != INVALID_SOCKET) closesocket(value); } };
void bounded_socket(SOCKET socket, uint32_t milliseconds) {
    if (!milliseconds || milliseconds > 60000 ||
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&milliseconds), sizeof(milliseconds)) ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&milliseconds), sizeof(milliseconds)))
        throw std::runtime_error("Could not set bounded private transport timeout.");
}
void connect_loopback(SOCKET socket, uint16_t port, uint32_t timeoutMs) {
    u_long nonblocking = 1; ioctlsocket(socket, FIONBIO, &nonblocking);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::runtime_error("Guest transport is not ready.");
        fd_set write, errors; FD_ZERO(&write); FD_ZERO(&errors); FD_SET(socket, &write); FD_SET(socket, &errors);
        timeval time{static_cast<long>(timeoutMs / 1000), static_cast<long>((timeoutMs % 1000) * 1000)};
        if (select(0, nullptr, &write, &errors, &time) <= 0 || FD_ISSET(socket, &errors))
            throw std::runtime_error("Private guest transport connection timed out.");
        int error = 0, length = sizeof(error);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) || error)
            throw std::runtime_error("Private guest transport connection failed.");
    }
    nonblocking = 0; ioctlsocket(socket, FIONBIO, &nonblocking); bounded_socket(socket, timeoutMs);
}
void transfer(SOCKET socket, char* bytes, size_t count, bool writing, uint64_t deadline) {
    while (count) {
        const auto now = GetTickCount64();
        if (now >= deadline) throw std::runtime_error("Private guest command deadline exceeded.");
        bounded_socket(socket, static_cast<uint32_t>(std::min<uint64_t>(deadline - now, 60000)));
        auto result = writing ? send(socket, bytes, static_cast<int>(std::min<size_t>(count, 65536)), 0) :
                                recv(socket, bytes, static_cast<int>(std::min<size_t>(count, 65536)), 0);
        if (result <= 0) throw std::runtime_error("Private guest transport closed or timed out.");
        bytes += result; count -= result;
    }
}
uint32_t word(const char* bytes) {
    uint32_t result = 0; for (unsigned i = 0; i < 4; ++i) result |= uint32_t(static_cast<unsigned char>(bytes[i])) << (8 * i); return result;
}
void put(char* bytes, uint32_t number) { for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<char>(number >> (8 * i)); }
constexpr uint32_t CNXN = 0x4e584e43, OPEN = 0x4e45504f, OKAY = 0x59414b4f, WRTE = 0x45545257, CLSE = 0x45534c43;
uint32_t checksum(const std::string& payload) { uint32_t result = 0; for (unsigned char byte : payload) result += byte; return result; }
struct Packet { uint32_t command, a, b; std::string payload; };
void send_packet(SOCKET socket, uint64_t deadline, uint32_t command, uint32_t a, uint32_t b, std::string payload = {}) {
    std::array<char, 24> bytes{}; put(bytes.data(), command); put(bytes.data() + 4, a); put(bytes.data() + 8, b);
    put(bytes.data() + 12, static_cast<uint32_t>(payload.size())); put(bytes.data() + 16, checksum(payload)); put(bytes.data() + 20, command ^ UINT32_MAX);
    transfer(socket, bytes.data(), bytes.size(), true, deadline); transfer(socket, payload.data(), payload.size(), true, deadline);
}
Packet read_packet(SOCKET socket, uint64_t deadline) {
    std::array<char, 24> bytes{}; transfer(socket, bytes.data(), bytes.size(), false, deadline);
    Packet result{word(bytes.data()), word(bytes.data() + 4), word(bytes.data() + 8), {}};
    uint32_t length = word(bytes.data() + 12);
    if (length > 256 * 1024 || word(bytes.data() + 20) != (result.command ^ UINT32_MAX))
        throw std::runtime_error("Invalid bounded ADB wire header.");
    result.payload.resize(length); transfer(socket, result.payload.data(), length, false, deadline);
    if (word(bytes.data() + 16) != checksum(result.payload)) throw std::runtime_error("Corrupt ADB wire payload.");
    return result;
}
void verify_target(const OwnedProcess& qemu, uint16_t port) {
    if (qemu.role != "qemu" || !port || !owned_process_is_live(qemu)) throw std::runtime_error("Scoped guest command requires its exact live QEMU.");
    verify_owned_endpoints({qemu}, {{qemu.pid, port}});
}
std::string line(SOCKET socket, uint64_t deadline) {
    std::string result;
    while (result.size() < 1048576) { char byte; transfer(socket, &byte, 1, false, deadline); if (byte == '\n') return result; result += byte; }
    throw std::runtime_error("Private control reply exceeds its size limit.");
}
} // namespace
struct PortReservation::Impl { Winsock stack; Socket socket; uint16_t port; };
PortReservation::PortReservation() : impl(std::make_unique<Impl>()) {
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        impl->socket.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        BOOL exclusive = TRUE;
        if (impl->socket.value == INVALID_SOCKET || setsockopt(impl->socket.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<char*>(&exclusive), sizeof(exclusive))) throw std::runtime_error("Could not reserve private runtime endpoint.");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(address);
        if (bind(impl->socket.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
            getsockname(impl->socket.value, reinterpret_cast<sockaddr*>(&address), &length))
            throw std::runtime_error("Could not reserve unique loopback runtime endpoint.");
        impl->port = ntohs(address.sin_port);
        if (impl->port != 5555 && impl->port != 4447 && impl->port != 4455 && impl->port != 5037) return;
        release();
    }
    throw std::runtime_error("Could not allocate an endpoint outside the development namespace.");
}
PortReservation::~PortReservation() = default;
uint16_t PortReservation::port() const { return impl->port; }
void PortReservation::release() { if (impl->socket.value != INVALID_SOCKET) { closesocket(impl->socket.value); impl->socket.value = INVALID_SOCKET; } }
ShellResult guest_shell(const OwnedProcess& qemu, uint16_t port, const std::string& command, uint32_t timeoutMs) {
    if (command.empty() || command.size() > 32768 || command.find('\0') != std::string::npos || !timeoutMs || timeoutMs > 60000)
        throw std::runtime_error("Invalid bounded private guest command.");
    Winsock stack; verify_target(qemu, port); Socket socket; socket.value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket.value == INVALID_SOCKET) throw std::runtime_error("Could not create private ADB transport.");
    auto deadline = GetTickCount64() + timeoutMs; connect_loopback(socket.value, port, timeoutMs); verify_target(qemu, port);
    std::string banner = "host::features=shell_v2;"; banner += '\0';
    send_packet(socket.value, deadline, CNXN, 0x01000000, 256 * 1024, banner);
    auto hello = read_packet(socket.value, deadline);
    // AOSP send_connect advertises its maximum version, not the negotiated
    // minimum. Android 17 replies 0x01000001 to our 0x01000000 greeting.
    // Keep advertising the original version: min(peer, host) stays 0x01000000
    // and ALL packets still require checksums. Do not infer skip-checksum from
    // the peer's maximum, accept AUTH/TLS, or accept unknown protocol versions.
    // https://android.googlesource.com/platform/packages/modules/adb/+/HEAD/adb.cpp
    if (hello.command != CNXN || (hello.a != 0x01000000 && hello.a != 0x01000001) ||
        hello.b < 4096 || !hello.payload.starts_with("device::"))
        throw std::runtime_error("Unsupported/secure ADB transport; expected this product's private debug adbd.");
    std::string service = "shell,v2,raw:" + command; service += '\0'; send_packet(socket.value, deadline, OPEN, 1, 0, service);
    auto opened = read_packet(socket.value, deadline);
    if (opened.command != OKAY || opened.b != 1 || !opened.a) throw std::runtime_error("Private adbd refused the scoped shell stream.");
    const auto remote = opened.a; std::string pending; ShellResult result{{}, {}, 0}; std::optional<uint32_t> exit;
    while (true) {
        auto packet = read_packet(socket.value, deadline);
        if (packet.a != remote || packet.b != 1) throw std::runtime_error("ADB stream identity changed.");
        if (packet.command == CLSE) {
            send_packet(socket.value, deadline, CLSE, 1, remote);
            if (!exit || !pending.empty()) throw std::runtime_error("Guest shell ended without a complete exit-status packet.");
            result.exitCode = *exit; return result;
        }
        if (packet.command != WRTE || pending.size() + packet.payload.size() > 1048576) throw std::runtime_error("Unexpected/oversized ADB stream packet.");
        pending += packet.payload; send_packet(socket.value, deadline, OKAY, 1, remote);
        while (pending.size() >= 5) {
            auto size = word(pending.data() + 1); if (size > 1048576) throw std::runtime_error("Guest shell reply exceeds its bound.");
            if (pending.size() < 5 + size) break;
            auto id = static_cast<unsigned char>(pending[0]);
            if ((id != 1 && id != 2 && id != 3) || exit || (id == 3 && size != 1)) throw std::runtime_error("Invalid shell-v2 channel/status ordering.");
            if (id == 3) exit = static_cast<unsigned char>(pending[5]);
            else {
                auto& output = id == 1 ? result.output : result.error;
                if (result.output.size() + result.error.size() + size > 1048576) throw std::runtime_error("Guest shell output exceeds its bound.");
                output.append(pending, 5, size);
            }
            pending.erase(0, 5 + size);
        }
    }
}
void guest_push_ota(const OwnedProcess& qemu,uint16_t port,const fs::path& source,const std::string& nonce,const Progress& progress) {
    if(!valid_instance_uuid(nonce))throw std::runtime_error("Invalid OTA inbox identity.");
    HANDLE input=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(input==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not lock the offline OTA package.");
    struct FileGuard{HANDLE h;~FileGuard(){CloseHandle(h);}} guard{input};BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(input,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))throw std::runtime_error("Offline OTA must be a regular nonredirected file.");
    uint64_t length=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;if(!length||length>(8ULL<<30))throw std::runtime_error("Offline OTA size exceeds bound.");
    const std::string path="/data/local/tmp/kiki-ota/incoming-"+nonce+".ota.zip";
    auto setup=guest_shell(qemu,port,"test ! -L /data/local/tmp/kiki-ota && mkdir -p /data/local/tmp/kiki-ota && chmod 755 /data/local/tmp/kiki-ota && test ! -e "+path,5000);
    if(setup.exitCode)throw std::runtime_error("Guest OTA inbox exists or is redirected.");
    Winsock stack;verify_target(qemu,port);Socket socket;socket.value=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(socket.value==INVALID_SOCKET)throw std::runtime_error("Could not create owned OTA transport.");auto deadline=GetTickCount64()+1800000;connect_loopback(socket.value,port,5000);verify_target(qemu,port);
    std::string banner="host::features=shell_v2;";banner+='\0';send_packet(socket.value,deadline,CNXN,0x01000000,256*1024,banner);auto hello=read_packet(socket.value,deadline);
    if(hello.command!=CNXN||(hello.a!=0x01000000&&hello.a!=0x01000001)||hello.b<4096||!hello.payload.starts_with("device::"))throw std::runtime_error("Offline OTA requires this exact private debug adbd.");
    std::string service="sync:";service+='\0';send_packet(socket.value,deadline,OPEN,1,0,service);auto open=read_packet(socket.value,deadline);if(open.command!=OKAY||open.b!=1||!open.a)throw std::runtime_error("Guest refused OTA sync.");auto remote=open.a;std::string reply;
    auto send=[&](const char* id,uint32_t size,const std::string& data){std::string body(8,'\0');std::copy_n(id,4,body.begin());put(body.data()+4,size);body+=data;send_packet(socket.value,deadline,WRTE,1,remote,body);
        for(;;){auto p=read_packet(socket.value,deadline);if(p.a!=remote||p.b!=1)throw std::runtime_error("OTA sync stream identity changed.");if(p.command==OKAY)break;if(p.command!=WRTE||reply.size()+p.payload.size()>4096)throw std::runtime_error("OTA sync stream failed.");reply+=p.payload;send_packet(socket.value,deadline,OKAY,1,remote);if(reply.starts_with("FAIL"))throw std::runtime_error("Guest rejected OTA staging: "+reply.substr(8));}};
    auto dest=path+",33188";send("SEND",static_cast<uint32_t>(dest.size()),dest);uint64_t sent=0,last=0;std::array<char,65536> buffer;
    while(sent<length){DWORD got=0;auto count=static_cast<DWORD>(std::min<uint64_t>(buffer.size(),length-sent));if(!ReadFile(input,buffer.data(),count,&got,nullptr)||got!=count)throw std::runtime_error("Offline OTA changed/truncated while staging.");send("DATA",got,std::string(buffer.data(),got));sent+=got;
        if(sent-last>=(16ULL<<20)){report(progress,"Staging signed full OTA: "+std::to_string(sent*100/length)+"%");verify_target(qemu,port);last=sent;}}
    send("DONE",0,{});
    while(reply.size()<8){auto p=read_packet(socket.value,deadline);if(p.command!=WRTE||p.a!=remote||p.b!=1||reply.size()+p.payload.size()>4096)throw std::runtime_error("Invalid OTA sync completion.");reply+=p.payload;send_packet(socket.value,deadline,OKAY,1,remote);}
    if(reply.size()!=8||reply.substr(0,4)!="OKAY"||word(reply.data()+4)!=0)throw std::runtime_error("Guest failed OTA sync completion.");
    send_packet(socket.value,deadline,CLSE,1,remote);verify_target(qemu,port);report(progress,"Offline package staged. The SAME KikiUpdater/update_engine will authenticate and install it.");
}
void qmp_powerdown(const OwnedProcess& qemu, uint16_t port) {
    Winsock stack; verify_target(qemu, port); Socket socket; socket.value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket.value == INVALID_SOCKET) throw std::runtime_error("Could not open scoped QMP control socket.");
    const auto deadline = GetTickCount64() + 5000; connect_loopback(socket.value, port, 5000); verify_target(qemu, port);
    auto greeting = parse_json_document(line(socket.value, deadline));
    if (!greeting.contains("QMP")) throw std::runtime_error("Private control endpoint is not QMP.");
    for (const char* command : {"qmp_capabilities", "system_powerdown"}) {
        auto request = json{{"execute", command}, {"id", command}}.dump() + "\r\n";
        transfer(socket.value, request.data(), request.size(), true, deadline);
        while (true) { auto reply = parse_json_document(line(socket.value, deadline));
            if (reply.contains("event")) continue;
            if (reply.value("id", "") != command || !reply.contains("return")) throw std::runtime_error("QMP refused scoped graceful powerdown.");
            break;
        }
    }
}
} // namespace kiki
