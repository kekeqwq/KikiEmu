#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include "surface_camera_winrt.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <streambuf>
#include <thread>
#include <utility>
#include <vector>

namespace {

class ScopedStreamBuffer final {
public:
    ScopedStreamBuffer(std::ostream& stream, std::streambuf* replacement)
        : stream_(stream), original_(stream.rdbuf(replacement)) {}
    ~ScopedStreamBuffer() { stream_.rdbuf(original_); }

    ScopedStreamBuffer(const ScopedStreamBuffer&) = delete;
    ScopedStreamBuffer& operator=(const ScopedStreamBuffer&) = delete;

private:
    std::ostream& stream_;
    std::streambuf* original_;
};

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) reset(std::exchange(other.value_, nullptr));
        return *this;
    }
    T* get() const { return value_; }
    T** put() { reset(); return &value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset(T* value = nullptr) {
        if (value_) value_->Release();
        value_ = value;
    }
private:
    T* value_ = nullptr;
};

std::string narrow(const wchar_t* value) {
    if (!value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string hrText(HRESULT hr) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08lx", static_cast<unsigned long>(hr));
    return buffer;
}

void check(HRESULT hr, std::string_view where) {
    if (FAILED(hr)) throw std::runtime_error(std::string(where) + " failed: " + hrText(hr));
}

class CameraLease {
public:
    CameraLease() {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\KikiEmu.SurfaceCameraLease.v1");
        if (!mutex_) throw std::runtime_error("Could not create the Surface camera ownership lock");
        auto state = WaitForSingleObject(mutex_, 0);
        if (state != WAIT_OBJECT_0 && state != WAIT_ABANDONED) {
            CloseHandle(mutex_); mutex_ = nullptr;
            throw std::runtime_error("Surface cameras are in use by another KikiEmu/Dev instance; close its camera first");
        }
    }
    ~CameraLease() { if (mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); } }
    CameraLease(const CameraLease&) = delete;
private:
    HANDLE mutex_ = nullptr;
};

struct CameraDevice {
    ~CameraDevice() {
        release();
    }

    HRESULT release() {
        if (released) return S_OK;
        winrtCamera.reset();
        reader.reset();
        HRESULT result = S_OK;
        if (source) {
            // Capture sources have an object-specific shutdown method. This is
            // the documented alternative to IMFActivate::ShutdownObject.
            result = source->Shutdown();
        }
        source.reset();
        activation.reset();
        // Release the shared camera lease only AFTER Windows capture sources.
        lease.reset();
        released = true;
        return result;
    }
    ComPtr<IMFActivate> activation;
    std::unique_ptr<CameraLease> lease;
    std::unique_ptr<SurfaceWinrtCamera> winrtCamera;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    bool released = false;
};

bool releaseCamera(std::unique_ptr<CameraDevice> *camera) {
    if (!*camera) return true;
    const HRESULT result = (*camera)->release();
    camera->reset();
    if (FAILED(result) && result != MF_E_SHUTDOWN) {
        std::cerr << "Could not completely release Surface camera: "
                  << hrText(result) << '\n';
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, ComPtr<IMFActivate>>> enumerateCameras() {
    ComPtr<IMFAttributes> attributes;
    check(MFCreateAttributes(attributes.put(), 1), "MFCreateAttributes");
    check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
          "Set video source type");

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    check(MFEnumDeviceSources(attributes.get(), &devices, &count), "MFEnumDeviceSources");

    std::vector<std::pair<std::string, ComPtr<IMFActivate>>> result;
    result.reserve(count);
    for (UINT32 i = 0; i < count; ++i) {
        WCHAR* name = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
            ComPtr<IMFActivate> activation;
            activation.reset(devices[i]);
            result.emplace_back(narrow(name), std::move(activation));
            CoTaskMemFree(name);
        } else {
            devices[i]->Release();
        }
    }
    CoTaskMemFree(devices);
    return result;
}

std::unique_ptr<CameraDevice> openCamera(std::string_view facing,
                                         uint32_t width, uint32_t height) {
    const std::string expected = facing == "front" ? "Surface Camera Front" :
                                 facing == "rear" ? "Surface Camera Rear" : "";
    if (expected.empty()) throw std::runtime_error("camera must be front or rear");
    auto lease = std::make_unique<CameraLease>();

    if (facing == "front") {
        auto camera = std::make_unique<CameraDevice>();
        camera->lease = std::move(lease);
        camera->winrtCamera = std::make_unique<SurfaceWinrtCamera>(facing, width, height);
        camera->width = camera->winrtCamera->width();
        camera->height = camera->winrtCamera->height();
        camera->stride = camera->width;
        return camera;
    }

    std::cerr << "openCamera(" << facing << "): enumerating devices\n";
    auto devices = enumerateCameras();
    std::cerr << "openCamera(" << facing << "): enumerated " << devices.size()
              << " camera(s)\n";
    auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& item) {
        return item.first == expected;
    });
    if (found == devices.end()) throw std::runtime_error(expected + " was not enumerated by Windows");

    auto camera = std::make_unique<CameraDevice>();
    camera->lease = std::move(lease);
    camera->activation = std::move(found->second);
    std::cerr << "openCamera(" << facing << "): activating source\n";
    check(camera->activation->ActivateObject(IID_PPV_ARGS(camera->source.put())),
          "Activate camera");
    std::cerr << "openCamera(" << facing << "): source active; creating SourceReader\n";
    check(MFCreateSourceReaderFromMediaSource(camera->source.get(), nullptr,
                                               camera->reader.put()),
          "MFCreateSourceReaderFromMediaSource");
    std::cerr << "openCamera(" << facing << "): SourceReader created; selecting NV12\n";

    ComPtr<IMFMediaType> mediaType;
    check(MFCreateMediaType(mediaType.put()), "MFCreateMediaType");
    check(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video major type");
    check(mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "Set NV12 output format");
    check(MFSetAttributeSize(mediaType.get(), MF_MT_FRAME_SIZE, width, height),
          "Set capture dimensions");
    check(MFSetAttributeRatio(mediaType.get(), MF_MT_FRAME_RATE, 30, 1),
          "Set capture frame rate");
    check(camera->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               nullptr, mediaType.get()),
          "Select NV12 capture mode");
    std::cerr << "openCamera(" << facing << "): NV12 selected; reading negotiated type\n";

    ComPtr<IMFMediaType> selectedType;
    check(camera->reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               selectedType.put()),
          "Get selected camera mode");
    check(MFGetAttributeSize(selectedType.get(), MF_MT_FRAME_SIZE,
                             &camera->width, &camera->height),
          "Read selected camera dimensions");
    UINT32 rawStride = 0;
    if (SUCCEEDED(selectedType->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride))) {
        camera->stride = static_cast<uint32_t>(std::abs(static_cast<LONG>(rawStride)));
    } else {
        LONG defaultStride = 0;
        check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_NV12.Data1,
                                              camera->width, &defaultStride),
              "Calculate NV12 stride");
        camera->stride = static_cast<uint32_t>(std::abs(defaultStride));
    }
    std::cerr << "openCamera(" << facing << "): ready " << camera->width << 'x'
              << camera->height << " stride=" << camera->stride << '\n';
    return camera;
}

std::vector<uint8_t> readNv12Frame(CameraDevice& camera, LONGLONG* timestamp) {
    if (camera.winrtCamera) {
        int64_t time = 0;
        auto frame = camera.winrtCamera->readFrame(&time);
        *timestamp = time;
        return frame;
    }
    ComPtr<IMFSample> sample;
    DWORD stream = 0;
    DWORD flags = 0;
    LONGLONG time = 0;
    for (int attempts = 0; attempts < 8 && !sample; ++attempts) {
        check(camera.reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                        &stream, &flags, &time, sample.put()),
              "Read camera frame");
        if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) {
            throw std::runtime_error("camera stream ended or returned an error");
        }
    }
    if (!sample) throw std::runtime_error("camera produced no frame");

    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(buffer.put()), "Get NV12 frame buffer");
    BYTE* input = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    check(buffer->Lock(&input, &maxLength, &currentLength), "Lock NV12 frame");

    const uint32_t rowStride = camera.stride;
    const uint64_t required = static_cast<uint64_t>(rowStride) * camera.height * 3 / 2;
    if (rowStride < camera.width || currentLength < required) {
        buffer->Unlock();
        throw std::runtime_error("camera returned an undersized NV12 buffer");
    }

    std::vector<uint8_t> compact(static_cast<size_t>(camera.width) * camera.height * 3 / 2);
    uint8_t* output = compact.data();
    for (uint32_t y = 0; y < camera.height; ++y) {
        std::memcpy(output + static_cast<size_t>(y) * camera.width,
                    input + static_cast<size_t>(y) * rowStride, camera.width);
    }
    const uint8_t* inputUv = input + static_cast<size_t>(rowStride) * camera.height;
    uint8_t* outputUv = output + static_cast<size_t>(camera.width) * camera.height;
    for (uint32_t y = 0; y < camera.height / 2; ++y) {
        std::memcpy(outputUv + static_cast<size_t>(y) * camera.width,
                    inputUv + static_cast<size_t>(y) * rowStride, camera.width);
    }
    buffer->Unlock();
    *timestamp = time;
    return compact;
}

uint64_t hostToNetwork64(uint64_t value) {
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32));
    const uint32_t low = htonl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32) | high;
}

#pragma pack(push, 1)
struct FrameHeader {
    char magic[4];
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t sequence;
    uint64_t timestamp100ns;
    uint32_t payloadBytes;
};
#pragma pack(pop)

bool sendAll(SOCKET socket, const void* data, size_t length) {
    const char* bytes = static_cast<const char*>(data);
    while (length) {
        const int chunk = static_cast<int>(std::min<size_t>(length, 1u << 20));
        const int sent = send(socket, bytes, chunk, 0);
        if (sent <= 0) return false;
        bytes += sent;
        length -= static_cast<size_t>(sent);
    }
    return true;
}

bool sendLine(SOCKET socket, const std::string& line) {
    return sendAll(socket, line.data(), line.size());
}

std::string readLine(SOCKET socket) {
    std::string line;
    char ch;
    while (line.size() < 256) {
        const int got = recv(socket, &ch, 1, 0);
        if (got <= 0) throw std::runtime_error("QEMU camera channel disconnected");
        if (ch == '\n') return line;
        if (ch != '\r') line.push_back(ch);
    }
    throw std::runtime_error("oversized command from Android camera HAL");
}

SOCKET connectToQemu(const char* host, const char* port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(host, port, &hints, &addresses) != 0)
        throw std::runtime_error("Could not resolve QEMU camera endpoint");
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addressGuard(addresses, freeaddrinfo);
    for (unsigned attempt = 0; attempt < 120; ++attempt) {
        for (auto* address = addresses; address; address = address->ai_next) {
            const SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (candidate == INVALID_SOCKET) continue;
            if (connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
                const BOOL noDelay = TRUE;
                setsockopt(candidate, IPPROTO_TCP, TCP_NODELAY,
                           reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
                return candidate;
            }
            closesocket(candidate);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    throw std::runtime_error("Could not connect to QEMU camera endpoint within 30 seconds");
}

int serve(const char* host, const char* port) {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) throw std::runtime_error("WSAStartup failed");
    const SOCKET socket = connectToQemu(host, port);
    std::unique_ptr<CameraDevice> active;
    std::string facing;
    uint32_t sequence = 0;
    std::cerr << "Connected to QEMU camera transport at " << host << ':' << port << '\n';
    try {
        for (;;) {
            const std::string command = readLine(socket);
            if (command == "CLOSE" || command.rfind("OPEN ", 0) == 0) {
                std::cerr << "QEMU camera command: " << command << '\n';
            }
            if (command.rfind("OPEN ", 0) == 0) {
                char requestedBuffer[16]{};
                uint32_t width = 0, height = 0;
                if (std::sscanf(command.c_str(), "OPEN %15s %u %u", requestedBuffer,
                                &width, &height) != 3) {
                    sendLine(socket, "ERROR malformed OPEN\n");
                    continue;
                }
                const std::string requested(requestedBuffer);
                if (!releaseCamera(&active)) {
                    sendLine(socket, "ERROR previous camera failed to release\n");
                    continue;
                }
                try {
                    active = openCamera(requested, width, height);
                    facing = requested;
                    sequence = 0;
                    sendLine(socket, "READY " + facing + " " +
                                      std::to_string(active->width) + " " +
                                      std::to_string(active->height) + " " +
                                      std::to_string(active->stride) + "\n");
                    std::cerr << "Opened Surface Camera " << facing << " at "
                              << active->width << 'x' << active->height << " NV12\n";
                } catch (const std::exception& error) {
                    std::cerr << "Surface camera " << requested
                              << " failed to open: " << error.what() << '\n';
                    sendLine(socket, "ERROR " + std::string(error.what()) + "\n");
                }
            } else if (command == "FRAME") {
                if (!active) {
                    std::cerr << "FRAME rejected: no active Surface camera\n";
                    sendLine(socket, "ERROR camera is not open\n");
                    continue;
                }
                LONGLONG timestamp = 0;
                try {
                    const std::vector<uint8_t> frame = readNv12Frame(*active, &timestamp);
                    const FrameHeader header{{'K', 'C', 'F', '1'}, htonl(active->width),
                        htonl(active->height), htonl(active->width), htonl(1),
                        htonl(sequence++), hostToNetwork64(static_cast<uint64_t>(timestamp)),
                        htonl(static_cast<uint32_t>(frame.size()))};
                    if (!sendAll(socket, &header, sizeof(header)) ||
                        !sendAll(socket, frame.data(), frame.size())) {
                        throw std::runtime_error("QEMU camera channel disconnected while sending frame");
                    }
                    if (sequence == 1 || sequence % 60 == 0) {
                        uint64_t luma = 0;
                        uint32_t samples = 0;
                        uint32_t signature = 2166136261U;
                        for (size_t pixel = 0; pixel < static_cast<size_t>(active->width) * active->height; pixel += 97) {
                            luma += frame[pixel];
                            ++samples;
                            signature = (signature ^ frame[pixel]) * 16777619U;
                        }
                        std::cerr << "Sent Surface " << facing << " frame #"
                                  << sequence - 1 << " bytes=" << frame.size()
                                  << " timestamp=" << timestamp
                                  << " meanY=" << (samples ? luma / samples : 0)
                                  << " signature=" << signature << '\n';
                    }
                } catch (const std::exception& error) {
                    std::cerr << "Surface " << facing << " frame failed: "
                              << error.what() << "; releasing host camera\n";
                    if (!releaseCamera(&active)) {
                        std::cerr << "Failed to release camera after capture error\n";
                    }
                    facing.clear();
                    sendLine(socket, "ERROR " + std::string(error.what()) + "\n");
                }
            } else if (command == "CLOSE") {
                if (!releaseCamera(&active)) {
                    sendLine(socket, "ERROR camera release failed\n");
                    continue;
                }
                facing.clear();
                std::cerr << "Released Surface camera before acknowledging CLOSE\n";
                sendLine(socket, "CLOSED\n");
            } else if (command == "PING") {
                sendLine(socket, "PONG\n");
            } else if (command == "QUIT") {
                releaseCamera(&active);
                sendLine(socket, "BYE\n");
                break;
            } else {
                sendLine(socket, "ERROR unknown command\n");
            }
        }
    } catch (...) {
        releaseCamera(&active);
        closesocket(socket);
        WSACleanup();
        throw;
    }
    releaseCamera(&active);
    closesocket(socket);
    WSACleanup();
    return 0;
}

int probe(std::string_view facing) {
    auto camera = openCamera(facing, 640, 480);
    LONGLONG timestamp = 0;
    auto frame = readNv12Frame(*camera, &timestamp);
    std::cout << "Captured and discarded one real Surface " << facing
              << " camera frame: " << camera->width << 'x' << camera->height
              << " NV12, " << frame.size() << " bytes\n";
    return 0;
}

int listCameraModes(std::string_view facing) {
    const std::string expected = facing == "front" ? "Surface Camera Front" :
                                 facing == "rear" ? "Surface Camera Rear" : "";
    if (expected.empty()) throw std::runtime_error("camera must be front or rear");

    auto devices = enumerateCameras();
    auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& item) {
        return item.first == expected;
    });
    if (found == devices.end()) throw std::runtime_error(expected + " was not enumerated by Windows");

    ComPtr<IMFMediaSource> source;
    check(found->second->ActivateObject(IID_PPV_ARGS(source.put())), "Activate camera for mode listing");
    ComPtr<IMFSourceReader> reader;
    check(MFCreateSourceReaderFromMediaSource(source.get(), nullptr, reader.put()),
          "Create SourceReader for mode listing");

    unsigned count = 0;
    for (DWORD index = 0; index < 64; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                       index, type.put());
        if (hr == MF_E_NO_MORE_TYPES) break;
        check(hr, "Enumerate native camera media type");
        GUID subtype{};
        UINT32 width = 0, height = 0, rateNum = 0, rateDen = 0;
        check(type->GetGUID(MF_MT_SUBTYPE, &subtype), "Read native camera subtype");
        MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &width, &height);
        MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &rateNum, &rateDen);
        std::cout << "mode=" << index << " size=" << width << 'x' << height
                  << " subtype=0x" << std::hex << subtype.Data1 << std::dec;
        if (rateNum && rateDen) {
            std::cout << " fps=" << rateNum << '/' << rateDen;
        }
        std::cout << '\n';
        ++count;
    }
    source->Shutdown();
    if (count == 0) throw std::runtime_error("camera exposed no native video media types");
    return 0;
}

int streamTest(std::string_view facing, unsigned frames,
               uint32_t width = 640, uint32_t height = 480) {
    if (frames == 0 || frames > 600) {
        throw std::runtime_error("stream-test frames must be between 1 and 600");
    }

    auto camera = openCamera(facing, width, height);
    for (unsigned i = 0; i < frames; ++i) {
        LONGLONG timestamp = 0;
        const auto frame = readNv12Frame(*camera, &timestamp);
        if (i == 0 || (i + 1) % 30 == 0 || i + 1 == frames) {
            std::cout << "Stream test " << facing << " frame " << (i + 1)
                      << '/' << frames << ": " << frame.size() << " NV12 bytes\n";
        }
    }
    std::cout << "Sustained " << facing << " camera capture passed.\n";
    return 0;
}

int switchStreamTest(unsigned frames) {
    if (frames == 0 || frames > 600) {
        throw std::runtime_error("switch-stream-test frames must be between 1 and 600");
    }

    bool firstCamera = true;
    for (const std::string_view facing : {"rear", "front", "rear"}) {
        if (!firstCamera) {
            check(MFShutdown(), "Reset Media Foundation before camera switch");
            CoUninitialize();
            std::this_thread::sleep_for(std::chrono::seconds(3));
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
                check(com, "Restart COM apartment before camera switch");
            }
            check(MFStartup(MF_VERSION), "Restart Media Foundation before camera switch");
        }
        firstCamera = false;
        auto camera = openCamera(facing, 640, 480);
        for (unsigned i = 0; i < frames; ++i) {
            LONGLONG timestamp = 0;
            const auto frame = readNv12Frame(*camera, &timestamp);
            if (i == 0 || i + 1 == frames) {
                std::cout << "Switch-stream test " << facing << " frame "
                          << (i + 1) << '/' << frames << ": " << frame.size()
                          << " NV12 bytes\n";
            }
        }
        camera.reset();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    std::cout << "Sustained rear/front/rear stream switching passed.\n";
    return 0;
}

int switchTest(unsigned rounds) {
    if (rounds == 0 || rounds > 20) {
        throw std::runtime_error("switch-test rounds must be between 1 and 20");
    }

    for (unsigned round = 1; round <= rounds; ++round) {
        for (const std::string_view facing : {"front", "rear"}) {
            // Destroy the previous IMFMediaSource before opening the other
            // physical camera. Surface's front/rear sensors share the camera
            // stack, so overlap would make a successful switch unreliable.
            auto camera = openCamera(facing, 640, 480);
            LONGLONG timestamp = 0;
            const auto frame = readNv12Frame(*camera, &timestamp);
            std::cout << "Switch test " << round << '/' << rounds << ": captured and discarded "
                      << facing << " frame " << camera->width << 'x' << camera->height
                      << " NV12 (" << frame.size() << " bytes)\n";
            camera.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::cout << "Front/rear sequential switching passed; each real camera was released "
                 "before the other was opened.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::ofstream cameraLog;
    std::unique_ptr<ScopedStreamBuffer> cameraLogRedirect;
    if (argc == 5 && std::string_view(argv[1]) == "--serve") {
        cameraLog.open(argv[4], std::ios::out | std::ios::app);
        if (!cameraLog) {
            std::cerr << "Could not open camera bridge log file: " << argv[4] << '\n';
            return 1;
        }
        cameraLogRedirect = std::make_unique<ScopedStreamBuffer>(std::cerr, cameraLog.rdbuf());
        std::cerr << "Camera bridge diagnostic log: " << argv[4] << '\n';
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        std::cerr << "CoInitializeEx failed: " << hrText(com) << '\n';
        return 1;
    }
    const HRESULT mf = MFStartup(MF_VERSION);
    if (FAILED(mf)) {
        std::cerr << "MFStartup failed: " << hrText(mf) << '\n';
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--list") {
            for (const auto& camera : enumerateCameras()) std::cout << camera.first << '\n';
        } else if (argc == 3 && std::string_view(argv[1]) == "--list-modes") {
            listCameraModes(argv[2]);
        } else if (argc == 3 && std::string_view(argv[1]) == "--probe") {
            probe(argv[2]);
        } else if (argc == 4 && std::string_view(argv[1]) == "--stream-test") {
            const unsigned frames = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
            streamTest(argv[2], frames);
        } else if (argc == 6 && std::string_view(argv[1]) == "--stream-test-size") {
            const unsigned frames = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
            const uint32_t width = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
            const uint32_t height = static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10));
            streamTest(argv[2], frames, width, height);
        } else if (argc == 3 && std::string_view(argv[1]) == "--switch-stream-test") {
            const unsigned frames = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
            switchStreamTest(frames);
        } else if ((argc == 2 || argc == 3) &&
                   std::string_view(argv[1]) == "--switch-test") {
            const unsigned rounds = argc == 3 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 3;
            switchTest(rounds);
        } else if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "--serve") {
            serve(argv[2], argv[3]);
        } else {
            std::cerr << "Usage: surface-camera-bridge --list | --list-modes front|rear | "
                         "--probe front|rear | "
                         "--stream-test front|rear frames | "
                         "--stream-test-size front|rear frames width height | "
                         "--switch-stream-test frames | "
                         "--switch-test [rounds] | --serve host port [log-file]\n";
            MFShutdown();
            if (SUCCEEDED(com)) CoUninitialize();
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
    MFShutdown();
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}
