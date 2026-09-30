#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include "surface_camera_winrt.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Graphics::Imaging;

namespace {
// The documented COM interface for access to SoftwareBitmap's locked planes.
struct MemoryBufferByteAccess : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(uint8_t** data, uint32_t* capacity) = 0;
};
constexpr GUID memoryBufferByteAccessId{0x5b0d3235, 0x4dba, 0x4d44,
                                      {0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d}};

template<class T> auto waitAsync(T const& operation) {
    auto done = std::make_shared<handle>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!*done) throw std::runtime_error("Could not create camera completion event");
    operation.Completed([done](auto const&, AsyncStatus) { SetEvent(done->get()); });
    HANDLE event = done->get();
    DWORD index = 0;
    check_hresult(CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
                                          15000, 1, &event, &index));
    return operation.GetResults();
}

std::string errorText(hresult_error const& error) {
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lx", static_cast<unsigned long>(error.code().value));
    return std::string(code) + ": " + to_string(error.message());
}

std::vector<uint8_t> copyNv12(SoftwareBitmap const& bitmap) {
    if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Nv12)
        throw std::runtime_error("WinRT camera returned a non-NV12 frame");
    const auto width = bitmap.PixelWidth();
    const auto height = bitmap.PixelHeight();
    if (width <= 0 || height <= 0 || width % 2 || height % 2)
        throw std::runtime_error("WinRT camera returned invalid NV12 dimensions");
    auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
    auto reference = buffer.CreateReference();
    com_ptr<MemoryBufferByteAccess> access;
    check_hresult(reference.as<::IUnknown>()->QueryInterface(memoryBufferByteAccessId, access.put_void()));
    uint8_t* input = nullptr;
    uint32_t capacity = 0;
    check_hresult(access->GetBuffer(&input, &capacity));
    if (!input) throw std::runtime_error("WinRT camera returned a null bitmap buffer");
    std::vector<uint8_t> compact(static_cast<size_t>(width) * height * 3 / 2);
    size_t outputOffset = 0;
    for (int planeIndex = 0; planeIndex < 2; ++planeIndex) {
        const auto plane = buffer.GetPlaneDescription(planeIndex);
        const int rows = planeIndex == 0 ? height : height / 2;
        for (int row = 0; row < rows; ++row) {
            const int64_t offset = static_cast<int64_t>(plane.StartIndex) + static_cast<int64_t>(row) * plane.Stride;
            if (offset < 0 || static_cast<uint64_t>(offset) + width > capacity)
                throw std::runtime_error("WinRT camera bitmap plane exceeds its buffer");
            std::memcpy(compact.data() + outputOffset, input + offset, width);
            outputOffset += width;
        }
    }
    reference.Close();
    buffer.Close();
    return compact;
}
} // namespace

struct SurfaceWinrtCamera::State : std::enable_shared_from_this<State> {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::atomic<bool> stop{false};
    bool initialized = false;
    std::exception_ptr initializeError;
    std::string captureError;
    uint32_t width = 0, height = 0;
    uint64_t sequence = 0, consumedSequence = 0;
    int64_t timestamp = 0;
    std::vector<uint8_t> latest;

    void run(std::string facing, uint32_t requestedWidth, uint32_t requestedHeight) noexcept {
        MediaCapture capture{nullptr};
        MediaFrameReader reader{nullptr};
        event_token failedToken{}, arrivedToken{};
        bool failedRegistered = false, arrivedRegistered = false, started = false;
        try {
            init_apartment(apartment_type::single_threaded);
            const auto devices = waitAsync(DeviceInformation::FindAllAsync(DeviceClass::VideoCapture));
            hstring deviceId;
            for (auto const& device : devices) {
                if (device.Name() == (facing == "front" ? L"Surface Camera Front" : L"Surface Camera Rear"))
                    deviceId = device.Id();
            }
            if (deviceId.empty()) throw std::runtime_error("Physical Surface camera not found");
            // Microsoft documents this profile as bypassing Windows Studio
            // Effects without changing host settings or replacing the sensor.
            MediaCaptureVideoProfile profile{nullptr};
            constexpr std::wstring_view passthrough = L"{E4ED96D9-CD40-412F-B20A-B7402A43DCD2}";
            for (auto const& candidate : MediaCapture::FindAllVideoProfiles(deviceId)) {
                if (std::wstring_view(candidate.Id()).starts_with(passthrough)) { profile = candidate; break; }
            }
            if (!profile) throw std::runtime_error("Surface no-effects color passthrough profile unavailable");
            capture = MediaCapture();
            failedToken = capture.Failed([this, keepAlive = shared_from_this()](auto const&, MediaCaptureFailedEventArgs const& args) {
                std::lock_guard lock(mutex);
                captureError = "Surface capture failed: " + to_string(args.Message());
                std::cerr << "WinRT camera failure 0x" << std::hex << args.Code() << std::dec
                          << ": " << captureError << '\n';
                changed.notify_all();
            });
            failedRegistered = true;
            MediaCaptureInitializationSettings settings;
            settings.VideoDeviceId(deviceId);
            settings.VideoProfile(profile);
            settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
            settings.StreamingCaptureMode(StreamingCaptureMode::Video);
            settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
            waitAsync(capture.InitializeAsync(settings));
            MediaFrameSource source{nullptr};
            for (auto const& entry : capture.FrameSources()) {
                if (entry.Value().Info().SourceKind() == MediaFrameSourceKind::Color &&
                    entry.Value().Info().MediaStreamType() == MediaStreamType::VideoRecord)
                    source = entry.Value();
            }
            if (!source) throw std::runtime_error("Surface color video source unavailable");
            MediaFrameFormat format{nullptr};
            for (auto const& candidate : source.SupportedFormats()) {
                if (candidate.VideoFormat().Width() == requestedWidth &&
                    candidate.VideoFormat().Height() == requestedHeight && candidate.Subtype() == L"NV12") {
                    format = candidate;
                    break;
                }
            }
            if (!format) throw std::runtime_error("Requested real camera NV12 mode is unavailable");
            waitAsync(source.SetFormatAsync(format));
            width = format.VideoFormat().Width();
            height = format.VideoFormat().Height();
            reader = waitAsync(capture.CreateFrameReaderAsync(source, L"NV12"));
            reader.AcquisitionMode(MediaFrameReaderAcquisitionMode::Realtime);
            arrivedToken = reader.FrameArrived([this, keepAlive = shared_from_this()](MediaFrameReader const& sender, auto const&) {
                MediaFrameReference frame{nullptr};
                SoftwareBitmap bitmap{nullptr};
                try {
                    frame = sender.TryAcquireLatestFrame();
                    if (frame) {
                        bitmap = frame.VideoMediaFrame().SoftwareBitmap();
                        if (!bitmap) throw std::runtime_error("CPU camera frame contains no SoftwareBitmap");
                        if (static_cast<uint32_t>(bitmap.PixelWidth()) != width ||
                            static_cast<uint32_t>(bitmap.PixelHeight()) != height)
                            throw std::runtime_error("Surface camera dimensions changed during capture");
                        auto bytes = copyNv12(bitmap);
                        const auto relativeTime = frame.SystemRelativeTime();
                        std::lock_guard lock(mutex);
                        latest = std::move(bytes);
                        timestamp = relativeTime ? relativeTime.Value().count() : 0;
                        ++sequence;
                        changed.notify_all();
                    }
                } catch (hresult_error const& error) {
                    std::lock_guard lock(mutex);
                    captureError = errorText(error);
                    changed.notify_all();
                } catch (std::exception const& error) {
                    std::lock_guard lock(mutex);
                    captureError = error.what();
                    changed.notify_all();
                }
                try { if (bitmap) bitmap.Close(); } catch (...) {}
                try { if (frame) frame.Close(); } catch (...) {}
            });
            arrivedRegistered = true;
            if (waitAsync(reader.StartAsync()) != MediaFrameReaderStartStatus::Success)
                throw std::runtime_error("Surface WinRT camera reader did not start");
            started = true;
            std::cerr << "WinRT " << facing << " camera ready " << width << 'x' << height
                      << " profile=" << to_string(profile.Id()) << '\n';
            {
                std::lock_guard lock(mutex);
                initialized = true;
                changed.notify_all();
            }
            while (!stop.load()) {
                handle timer{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                HANDLE event = timer.get();
                DWORD index = 0;
                CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
                                         50, 1, &event, &index);
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            initializeError = std::current_exception();
            initialized = true;
            changed.notify_all();
        }
        // StopAsync quiesces callbacks before closing capture and joining this
        // STA. No camera or frame object outlives CLOSE/camera destruction.
        try { if (started) waitAsync(reader.StopAsync()); } catch (...) {}
        try { if (arrivedRegistered) reader.FrameArrived(arrivedToken); } catch (...) {}
        try { if (reader) reader.Close(); } catch (...) {}
        try { if (failedRegistered) capture.Failed(failedToken); } catch (...) {}
        try { if (capture) capture.Close(); } catch (...) {}
        std::cerr << "WinRT " << facing << " camera released\n";
        uninit_apartment();
    }
};

SurfaceWinrtCamera::SurfaceWinrtCamera(std::string_view facing, uint32_t width, uint32_t height)
    : state_(std::make_shared<State>()) {
    state_->worker = std::thread([state = state_, facing = std::string(facing), width, height] {
        state->run(facing, width, height);
    });
    std::unique_lock lock(state_->mutex);
    const bool ready = state_->changed.wait_for(lock, std::chrono::seconds(30), [&] { return state_->initialized; });
    const auto error = state_->initializeError;
    lock.unlock();
    if (!ready || error) {
        state_->stop = true;
        state_->worker.join();
        if (error) {
            try { std::rethrow_exception(error); }
            catch (hresult_error const& e) { throw std::runtime_error(errorText(e)); }
        }
        throw std::runtime_error("Surface WinRT camera initialization timed out");
    }
}

SurfaceWinrtCamera::~SurfaceWinrtCamera() {
    state_->stop = true;
    if (state_->worker.joinable()) state_->worker.join();
}
uint32_t SurfaceWinrtCamera::width() const { return state_->width; }
uint32_t SurfaceWinrtCamera::height() const { return state_->height; }
std::vector<uint8_t> SurfaceWinrtCamera::readFrame(int64_t* timestamp) {
    std::unique_lock lock(state_->mutex);
    if (!state_->changed.wait_for(lock, std::chrono::seconds(5), [&] {
        return state_->sequence > state_->consumedSequence || !state_->captureError.empty();
    })) throw std::runtime_error("Surface camera stopped producing new frames");
    if (!state_->captureError.empty()) throw std::runtime_error(state_->captureError);
    state_->consumedSequence = state_->sequence;
    *timestamp = state_->timestamp;
    return state_->latest;
}
