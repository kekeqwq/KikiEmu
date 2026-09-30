#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// Native Windows ARM64 camera capture. Images are never written to disk.
class SurfaceWinrtCamera final {
public:
    SurfaceWinrtCamera(std::string_view facing, uint32_t width, uint32_t height);
    ~SurfaceWinrtCamera();
    SurfaceWinrtCamera(const SurfaceWinrtCamera&) = delete;
    SurfaceWinrtCamera& operator=(const SurfaceWinrtCamera&) = delete;
    uint32_t width() const;
    uint32_t height() const;
    std::vector<uint8_t> readFrame(int64_t* timestamp);
private:
    struct State;
    std::shared_ptr<State> state_;
};
