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
#include <winrt/Windows.Storage.Streams.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;

// Pump the STA during camera initialization; do not block a WinRT .get() on it.
template<class T> auto waitAsync(T const& operation) {
    handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    operation.Completed([event = done.get()](auto const&, AsyncStatus) { SetEvent(event); });
    HANDLE event = done.get();
    DWORD index = 0;
    check_hresult(CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
                                          20000, 1, &event, &index));
    return operation.GetResults();
}

int main(int argc, char** argv) {
    try {
        init_apartment(apartment_type::single_threaded);
        const bool front = argc < 2 || std::string(argv[1]) == "front";
        const bool cpu = argc < 3 || std::string(argv[2]) == "cpu";
        const unsigned seconds = argc < 4 ? 12 : std::stoul(argv[3]);
        const auto cameras = waitAsync(DeviceInformation::FindAllAsync(DeviceClass::VideoCapture));
        hstring deviceId;
        for (auto const& device : cameras) {
            if (device.Name() == (front ? L"Surface Camera Front" : L"Surface Camera Rear")) {
                deviceId = device.Id();
            }
        }
        if (deviceId.empty()) throw std::runtime_error("Surface camera not found");
        const auto profiles = MediaCapture::FindAllVideoProfiles(deviceId);
        unsigned profileIndex = 0;
        for (auto const& profile : profiles) {
            std::cout << "PROFILE index=" << profileIndex++ << " id=" << to_string(profile.Id())
                      << " preview=" << profile.SupportedPreviewMediaDescription().Size()
                      << " record=" << profile.SupportedRecordMediaDescription().Size()
                      << " photo=" << profile.SupportedPhotoMediaDescription().Size() << '\n';
        }
        if (argc >= 5 && std::string(argv[4]) == "profiles") return 0;
        MediaCapture capture;
        auto failureToken = capture.Failed([](auto const&, MediaCaptureFailedEventArgs const& args) {
            std::cerr << "CAPTURE_FAILED code=0x" << std::hex << args.Code() << std::dec
                      << " message=" << to_string(args.Message()) << '\n';
        });
        MediaCaptureInitializationSettings settings;
        settings.VideoDeviceId(deviceId);
        settings.StreamingCaptureMode(StreamingCaptureMode::Video);
        settings.MemoryPreference(cpu ? MediaCaptureMemoryPreference::Cpu : MediaCaptureMemoryPreference::Auto);
        if (argc >= 5) {
            const auto selected = profiles.GetAt(std::stoul(argv[4]));
            settings.VideoProfile(selected);
            std::cout << "SELECTED_PROFILE " << to_string(selected.Id()) << '\n';
        }
        waitAsync(capture.InitializeAsync(settings));
        MediaFrameSource source{nullptr};
        for (auto const& entry : capture.FrameSources()) {
            if (entry.Value().Info().SourceKind() == MediaFrameSourceKind::Color) source = entry.Value();
        }
        if (!source) throw std::runtime_error("Color source unavailable");
        for (auto const& format : source.SupportedFormats()) {
            if (format.VideoFormat().Width() == 640 && format.VideoFormat().Height() == 480 &&
                format.Subtype() == L"NV12") {
                waitAsync(source.SetFormatAsync(format));
                break;
            }
        }
        auto reader = waitAsync(capture.CreateFrameReaderAsync(source, L"NV12"));
        reader.AcquisitionMode(MediaFrameReaderAcquisitionMode::Realtime);
        std::atomic<unsigned> frames{0}, changed{0}, nullFrames{0};
        std::mutex statsLock;
        int64_t lastTimestamp = -1;
        auto arrivedToken = reader.FrameArrived([&](MediaFrameReader const& sender, auto const&) {
            try {
                auto frame = sender.TryAcquireLatestFrame();
                if (!frame) { ++nullFrames; return; }
                auto bitmap = frame.VideoMediaFrame().SoftwareBitmap();
                if (bitmap) {
                    Buffer bytes(bitmap.PixelWidth() * bitmap.PixelHeight() * 4);
                    bitmap.CopyToBuffer(bytes);
                    bitmap.Close();
                }
                auto time = frame.SystemRelativeTime();
                const auto timestamp = time ? time.Value().count() : -1;
                {
                    std::lock_guard lock(statsLock);
                    if (timestamp != lastTimestamp) ++changed;
                    lastTimestamp = timestamp;
                }
                frame.Close();
                const auto n = ++frames;
                if (n == 1 || n % 30 == 0) {
                    std::cout << "FRAME count=" << n << " unique_times=" << changed
                              << " timestamp=" << timestamp << std::endl;
                }
            } catch (hresult_error const& error) {
                std::cerr << "FRAME_ERROR code=0x" << std::hex << static_cast<uint32_t>(error.code().value)
                          << std::dec << " message=" << to_string(error.message()) << '\n';
            }
        });
        const auto status = waitAsync(reader.StartAsync());
        std::cout << "START facing=" << (front ? "front" : "rear") << " cpu=" << cpu
                  << " status=" << static_cast<int>(status) << std::endl;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD index;
            handle timer{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            HANDLE event = timer.get();
            CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
                                     50, 1, &event, &index);
        }
        waitAsync(reader.StopAsync());
        reader.FrameArrived(arrivedToken);
        reader.Close();
        capture.Failed(failureToken);
        capture.Close();
        std::cout << "RELEASED frames=" << frames << " unique_times=" << changed
                  << " null=" << nullFrames << std::endl;
        return frames > seconds * 10 ? 0 : 1;
    } catch (hresult_error const& error) {
        std::cerr << "ERROR hr=0x" << std::hex << static_cast<uint32_t>(error.code().value)
                  << std::dec << " message=" << to_string(error.message()) << '\n';
        return 2;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
