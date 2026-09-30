// SPDX-License-Identifier: GPL-2.0-or-later
// Internal fake adbd. This file is never shipped or invoked by the launcher.
#pragma once
#include <array>
#include <fstream>

namespace wire_fixture {
inline uint32_t word(const char* p) {
    uint32_t n = 0; for (unsigned i = 0; i < 4; ++i) n |= uint32_t(static_cast<unsigned char>(p[i])) << (8 * i); return n;
}
inline void put(char* p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<char>(n >> (8 * i)); }
inline uint32_t sum(const std::string& text) { uint32_t n = 0; for (unsigned char c : text) n += c; return n; }
inline void bytes(SOCKET s, char* p, size_t size, bool sendBytes) {
    while (size) {
        int n = sendBytes ? send(s, p, static_cast<int>(size), 0) : recv(s, p, static_cast<int>(size), 0);
        if (n <= 0) throw std::runtime_error("Fake wire peer disconnected.");
        p += n; size -= n;
    }
}
struct Packet { uint32_t command, a, b; std::string payload; };
inline Packet read(SOCKET s) {
    std::array<char, 24> h{}; bytes(s, h.data(), h.size(), false);
    Packet p{word(h.data()), word(h.data() + 4), word(h.data() + 8), {}};
    auto size = word(h.data() + 12);
    if (size > 65536 || word(h.data() + 20) != (p.command ^ UINT32_MAX)) throw std::runtime_error("Invalid fake peer header.");
    p.payload.resize(size); bytes(s, p.payload.data(), size, false);
    if (word(h.data() + 16) != sum(p.payload)) throw std::runtime_error("Invalid fake peer checksum.");
    return p;
}
inline void write(SOCKET s, uint32_t command, uint32_t a, uint32_t b, std::string text, bool corrupt = false) {
    std::array<char, 24> h{}; put(h.data(), command); put(h.data() + 4, a); put(h.data() + 8, b);
    put(h.data() + 12, static_cast<uint32_t>(text.size())); put(h.data() + 16, sum(text) + corrupt);
    put(h.data() + 20, command ^ UINT32_MAX);
    bytes(s, h.data(), h.size(), true); bytes(s, text.data(), text.size(), true);
}
inline std::string channel(unsigned char id, const std::string& text) {
    std::string out(5, '\0'); out[0] = id; put(out.data() + 1, static_cast<uint32_t>(text.size())); return out + text;
}
inline int serve(uint16_t port, const std::wstring& mode, const kiki::fs::path& ready) {
    WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) return 20;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), peer = INVALID_SOCKET;
    try {
        BOOL exclusive = TRUE;
        setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 1)) return 21;
        { std::ofstream out(ready); out << "READY\n"; if (!out) return 22; }
        peer = accept(listener, nullptr, nullptr);
        DWORD timeout = 5000; setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        if (mode == L"timeout") { Sleep(1000); }
        else {
            constexpr uint32_t cnxn = 0x4e584e43, open = 0x4e45504f, okay = 0x59414b4f, wrte = 0x45545257, clse = 0x45534c43;
            auto hello = read(peer);
            if (hello.command != cnxn || hello.a != 0x01000000 || !hello.payload.starts_with("host::features=shell_v2;")) return 23;
            std::string banner = "device::ro.product.name=kikiaosp_test;features=shell_v2;"; banner += '\0';
            write(peer, cnxn, 0x01000000, 65536, banner, mode == L"corrupt");
            if (mode != L"corrupt") {
                auto request = read(peer);
                if (request.command != open || request.a != 1 || request.b != 0 || request.payload != std::string("shell,v2,raw:echo fixture") + '\0') return 24;
                write(peer, okay, 7, 1, {});
                auto body = channel(1, "fixture\n") + channel(2, "warning\n") + channel(3, std::string(1, '\x07'));
                if (mode == L"no-exit") body = channel(1, "fixture\n");
                // Fragment a shell-v2 header across transport packets, and
                // coalesce stdout/stderr/exit in the remaining packet.
                for (const auto& piece : {body.substr(0, 3), body.substr(3)}) {
                    write(peer, wrte, 7, mode == L"foreign-stream" ? 2 : 1, piece);
                    auto ack = read(peer); if (ack.command != okay || ack.a != 1 || ack.b != 7) return 25;
                }
                write(peer, clse, 7, 1, {}); auto close = read(peer);
                if (close.command != clse || close.a != 1 || close.b != 7) return 26;
            }
        }
        if (peer != INVALID_SOCKET) closesocket(peer); closesocket(listener); WSACleanup(); return 0;
    } catch (...) {
        if (peer != INVALID_SOCKET) closesocket(peer); if (listener != INVALID_SOCKET) closesocket(listener); WSACleanup();
        // Negative cases deliberately disconnect after rejecting the peer.
        return mode == L"success" ? 27 : 0;
    }
}
} // namespace wire_fixture
