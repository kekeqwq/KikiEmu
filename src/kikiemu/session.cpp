// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "session.hpp"
#include "disk.hpp"
#include <cmath>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
std::string trim(std::string text) {
    auto first = text.find_first_not_of(" \t\r\n"); if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
std::wstring qemu_value(const fs::path& path) {
    // QEMU comma-separated key/value options escape literal commas as ',,'.
    std::wstring result; for (auto c : path.wstring()) { result += c; if (c == L',') result += c; } return result;
}
fs::path session_logs(const json& record) {
    const auto uuid = record.at("runtime").at("sessionUuid").get<std::string>();
    if (!valid_instance_uuid(uuid)) throw std::runtime_error("Invalid registered session identity.");
    return parse_storage_identity(record.at("owner")).directory / "logs" / utf16(uuid);
}
void write_status(const fs::path& log, const std::string& state, std::string stage, std::string verified = {}) {
    auto safe = [](std::string value) {
        std::string out; for (unsigned char c : value) { if (c == '\r' || c == '\n') out += ' '; else if (c == '\\') out += "\\\\"; else out += c; } return out;
    };
    auto pending = log / "boot.pending.ini";
    { std::ofstream output(pending, std::ios::binary | std::ios::trunc);
      output << "[boot]\nstate=" << state << "\nstage=" << safe(stage) << "\nverified=" << safe(verified) << '\n';
      if (!output) throw std::runtime_error("Could not write owned boot status."); }
    if (!MoveFileExW(pending.c_str(), (log / "boot.ini").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not publish complete owned boot status.");
    std::ofstream setup(log / "setup.log", std::ios::app); setup << state << ": " << stage << ' ' << verified << '\n';
}
OwnedProcess qemu_process(const json& record) {
    if (record.at("runtime").is_null()) throw std::runtime_error("Instance is not running.");
    std::optional<OwnedProcess> found;
    for (const auto& value : record.at("runtime").at("processes")) {
        auto process = parse_owned_process(value);
        if (process.role == "qemu") { if (found) throw std::runtime_error("Duplicate runtime QEMU identity."); found = process; }
    }
    if (!found || !owned_process_is_live(*found)) throw std::runtime_error("Instance has no live owned QEMU.");
    return *found;
}
uint16_t endpoint(const json& record, const char* role, uint32_t pid) {
    uint16_t port = 0;
    for (const auto& value : record.at("runtime").at("endpoints")) {
        if (value.value("role", "") != role) continue;
        auto number = value.at("port").get<uint64_t>();
        if (port || value.at("pid") != pid || !number || number > 65535) throw std::runtime_error("Invalid scoped runtime endpoint.");
        port = static_cast<uint16_t>(number);
    }
    if (!port) throw std::runtime_error("Instance has no registered scoped endpoint."); return port;
}
bool alive(const json& runtime) {
    if (runtime.is_null()) return false;
    for (const auto& value : runtime.at("processes")) if (owned_process_is_live(parse_owned_process(value))) return true;
    return false;
}
class RuntimePins {
    std::vector<HANDLE> handles;
public:
    ~RuntimePins() { for (auto handle : handles) CloseHandle(handle); }
    void add(const fs::path& path, bool mutableFile = false) {
        const auto share = FILE_SHARE_READ | (mutableFile ? FILE_SHARE_WRITE : 0);
        auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, share, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not pin an owned runtime path.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            CloseHandle(handle); throw std::runtime_error("Runtime path is redirected; launch refused.");
        }
        handles.push_back(handle);
    }
};
void same_session(const json& record, const std::string& session) {
    if (record.at("runtime").is_null() || record.at("runtime").value("sessionUuid", "") != session ||
        !record.at("deleteJournal").is_null()) throw std::runtime_error("Session no longer owns this instance.");
}
template<class Function> void update(const ManagerPaths& paths, const std::string& id, const std::string& session, Function function) {
    // Concurrent management locks are short-lived. Do not hold a registry
    // lock while waiting on Android, a process, a camera or network I/O.
    const auto deadline = GetTickCount64() + 10000;
    for (;;) {
        try {
            RegistryTransaction registry(paths.registryRoot, paths.channel);
            auto& record = registry.instance(id); same_session(record, session); function(record); registry.save(); return;
        } catch (const std::exception& error) {
            if (std::string(error.what()).find("Another manager operation is active") == std::string::npos || GetTickCount64() >= deadline) throw;
            Sleep(50);
        }
    }
}
BOOL CALLBACK activate(HWND window, LPARAM parameter) {
    auto process = reinterpret_cast<const OwnedProcess*>(parameter); DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid != process->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window); return FALSE;
}
} // namespace
bool launcher_display_ready(const std::string& windows, const std::string& layers, const std::string& display) {
    static const std::regex focusedHome(
        R"((?:^|\n)[ \t]*mCurrentFocus=Window\{[^\r\n]*[ \t]com\.android\.launcher3/(?:com\.android\.launcher3\.uioverrides\.QuickstepLauncher|\.uioverrides\.QuickstepLauncher)(?:[ \t}]|$))");
    static const std::regex active120(
        R"((?:^|\n)[ \t]*mActiveRenderFrameRate=([0-9]+(?:\.[0-9]+)?)[ \t\r]*(?:\n|$))");
    std::smatch rate;
    if (!std::regex_search(display, rate, active120) || rate[1].length() > 32) return false;
    try {
        // Real Android reports 120.00001 for the 8,333,333-ns 120-Hz mode.
        // Allow only tiny representation/period rounding, not 120.01 or an
        // arbitrary supported-mode list that happens to mention 120.
        if (std::abs(std::stod(rate[1].str()) - 120.0) > 0.0001) return false;
    } catch (const std::exception&) { return false; }
    return std::regex_search(windows, focusedHome) &&
        layers.find("com.android.launcher3/com.android.launcher3.uioverrides.QuickstepLauncher") != std::string::npos;
}
RuntimePlan runtime_plan(const json& record, const ManagerPaths& paths, const std::string& session,
                         uint16_t adb, uint16_t qmp, uint16_t camera) {
    auto owner = parse_storage_identity(record.at("owner"));
    if (!valid_instance_uuid(session) || owner.channel != paths.channel || !adb || !qmp || !camera ||
        adb == qmp || adb == camera || qmp == camera)
        throw std::runtime_error("Runtime plan requires an owned channel and three unique endpoints.");
    for (auto port : {adb, qmp, camera}) if (port == 5555 || port == 4447 || port == 4455 || port == 5037)
        throw std::runtime_error("Runtime cannot reuse development/global ADB ports.");
    const auto& layout = record.at("immutableSource").at("layout");
    auto bootUuid = layout.at("partitions").at(0).at("uuid").get<std::string>();
    if (layout.at("partitions").at(0).at("name") != "boot" || !valid_instance_uuid(bootUuid))
        throw std::runtime_error("Runtime requires the installed boot partition UUID.");
    const auto& resources = record.at("configuration").at("resources");
    const auto cpus = resources.at("cpus").get<uint32_t>();
    const auto memoryMiB = resources.at("memoryBytes").get<uint64_t>() >> 20;
    if (!cpus || cpus > 64 || memoryMiB < 1024) throw std::runtime_error("Unsupported runtime resource budget.");
    auto bin = fs::path(utf16(record.at("configuration").at("qemu").at("binDirectory").get<std::string>()));
    RuntimePlan plan{bin / "qemu-system-aarch64.exe", paths.appRoot / "surface-camera-bridge.exe", owner.directory,
                     owner.directory / "logs" / utf16(session), {}, {}, "kiki-" + paths.channel + "-" + owner.instanceUuid};
    std::wstring command = L"earlycon=pl011,0x09000000 console=ttyAMA0 loglevel=8 printk.devkmsg=on audit=0 "
        L"androidboot.hardware=ranchu androidboot.hardwareegl=mesa androidboot.hardware.egl=mesa "
        L"androidboot.hardware.gralloc=minigbm androidboot.hardware.hwcomposer=ranchu androidboot.hardware.vulkan=pastel "
        L"androidboot.hardware.hwcomposer.mode=client androidboot.hardware.hwcomposer.display_finder_mode=drm "
        L"androidboot.hardware.guest_hwui_renderer=gles androidboot.debug.renderengine.backend=skiaglthreaded "
        L"androidboot.selinux=permissive enforcing=0 androidboot.force_normal_boot=1 androidboot.verifiedbootstate=orange "
        L"androidboot.init_fatal_reboot_target=none androidboot.adb.secure=0 binder.devices=binder,hwbinder,vndbinder "
        L"androidboot.boot_part_uuid=" + utf16(bootUuid) + L" androidboot.serialno=" + utf16(plan.serial);
    auto log = fs::path("logs") / utf16(session);
    plan.arguments = {L"-L", (bin / "roms").wstring(), L"-M", L"virt", L"-accel", L"whpx", L"-cpu", L"host",
        L"-m", std::to_wstring(memoryMiB), L"-smp", std::to_wstring(cpus), L"-parallel", L"none",
        L"-kernel", L"disk/boot/kernel", L"-initrd", L"disk/boot/ramdisk.img", L"-append", command,
        L"-drive", L"if=none,file=disk/phone.qcow2,format=qcow2,id=kiki-phone,discard=unmap,detect-zeroes=unmap",
        L"-device", L"virtio-blk-pci,drive=kiki-phone", L"-device", L"virtio-gpu-gl-pci,hostmem=256M,xres=1003,yres=1556",
        L"-device", L"virtio-multitouch-pci", L"-device", L"virtio-keyboard-pci",
        L"-netdev", L"user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3,hostfwd=tcp:127.0.0.1:" + std::to_wstring(adb) + L"-:5555",
        L"-device", L"virtio-net-pci,netdev=net0", L"-device", L"virtio-serial-pci,id=kiki-serial",
        L"-chardev", L"file,id=kiki-logcat,path=" + qemu_value(log / "logcat.log"),
        L"-device", L"virtconsole,chardev=kiki-logcat,bus=kiki-serial.0,name=org.kikiaosp.logcat",
        L"-chardev", L"socket,id=kiki-camera,host=127.0.0.1,port=" + std::to_wstring(camera) + L",server=on,wait=off",
        L"-device", L"virtserialport,bus=kiki-serial.0,chardev=kiki-camera,name=org.kikiaosp.camera",
        L"-display", L"sdl,gl=on", L"-qmp", L"tcp:127.0.0.1:" + std::to_wstring(qmp) + L",server=on,wait=off",
        L"-serial", L"file:" + (log / "serial.log").wstring(),
        L"-audiodev", L"sdl,id=kiki_audio,out.buffer-length=20000,out.buffer-count=4",
        L"-device", L"virtio-sound-pci,audiodev=kiki_audio,streams=1"};
    plan.environment = {{L"KIKI_SDL_GUEST_REFRESH_RATE_HZ", L"120"}, {L"KIKI_SDL_SWAP_INTERVAL", L"0"},
        {L"KIKI_SDL_DISABLE_GRAB", L"1"}, {L"KIKI_SDL_DISABLE_IME", L"1"}, {L"KIKI_SDL_RAW_KEYBOARD_TRACE", L"0"},
        {L"KIKI_SDL_NATIVE_PIXELS", L"1"}, {L"KIKI_SDL_START_WIDTH", L"1003"}, {L"KIKI_SDL_START_HEIGHT", L"1556"},
        {L"KIKI_SDL_WINDOW_TITLE", paths.channel == "release" ? L"KikiEmu" : L"QEMU/Dev"},
        {L"KIKI_SDL_BOOT_STATUS", (plan.logDirectory / "boot.ini").wstring()},
        {L"KIKI_SDL_BOOT_EVENTS", (plan.logDirectory / "boot-events.log").wstring()},
        {L"KIKI_SDL_BOOT_SERIAL", (plan.logDirectory / "serial.log").wstring()},
        {L"KIKI_SDL_BOOT_LOGCAT", (plan.logDirectory / "logcat.log").wstring()},
        {L"KIKI_SDL_BOOT_SETUP", (plan.logDirectory / "setup.log").wstring()},
        {L"KIKI_SDL_BOOT_DETAILS", L"KikiAOSP 0.1 Alpha | SDL / VirGL / 120 Hz | " + std::to_wstring(cpus) +
            L" vCPU / " + std::to_wstring(memoryMiB) + L" MiB | " + utf16(paths.channel)}};
    return plan;
}
std::string start_instance(const ManagerPaths& paths, const std::string& requested) {
    RegistryTransaction registry(paths.registryRoot, paths.channel);
    std::string id = requested;
    if (id.empty()) {
        if (registry.state().at("defaultId").is_null()) throw std::runtime_error("No default system. Initialize a system with kikiemu create, then use kikiemu set --default ID.");
        id = registry.state().at("defaultId").get<std::string>();
    }
    auto& record = registry.instance(id);
    if (!record.at("deleteJournal").is_null()) throw std::runtime_error("This instance has an unfinished deletion; start refused.");
    if (alive(record.at("runtime"))) {
        try { auto process = qemu_process(record); EnumWindows(activate, reinterpret_cast<LPARAM>(&process)); } catch (...) {}
        return "Instance " + id + " is already " + record.at("lifecycle").get<std::string>() + ".\n";
    }
    auto owner = parse_storage_identity(record.at("owner"));
    auto bin = fs::path(utf16(record.at("configuration").at("qemu").at("binDirectory").get<std::string>()));
    StorageLease lease(owner, {paths.appRoot, paths.registryRoot, bin}); verify_qemu_binding(record.at("configuration").at("qemu"));
    auto session = new_instance_uuid(); auto logs = owner.directory / "logs" / utf16(session);
    auto logRoot = owner.directory / "logs";
    if (!fs::exists(logRoot) && !fs::create_directory(logRoot)) throw std::runtime_error("Could not create owned logs directory.");
    RuntimePins logPins; logPins.add(logRoot, true);
    if (!fs::create_directory(logs)) throw std::runtime_error("New session log directory already exists.");
    logPins.add(logs, true);
    write_status(logs, "WAITING", "Starting the isolated system supervisor");
    auto executable = paths.appRoot / "kikiemu-desktop.exe";
    if (inspect_pe(executable).machine != 0xaa64) throw std::runtime_error("The installed desktop supervisor must be native ARM64.");
    PrivateChild supervisor(executable, owner.directory,
        {L"--supervise", L"--id", utf16(id), L"--session", utf16(session)}, {}, logs / "supervisor.log", false);
    auto process = describe_owned_process(supervisor.pid(), executable, owner.instanceUuid, paths.channel, "supervisor");
    record["runtime"] = {{"instanceUuid", owner.instanceUuid}, {"channel", paths.channel}, {"sessionUuid", session},
                         {"processes", json::array({owned_process_json(process)})}, {"endpoints", json::array()}};
    record["lifecycle"] = "starting"; record["lastError"] = nullptr; registry.save();
    try { supervisor.resume(); }
    catch (...) { record["runtime"] = nullptr; record["lifecycle"] = "idle"; registry.save(); throw; }
    return "Starting instance " + id + ".\n";
}
std::string stop_instance(const ManagerPaths& paths, const std::string& id) {
    json record;
    { RegistryTransaction registry(paths.registryRoot, paths.channel); record = registry.instance(id); }
    auto process = qemu_process(record); auto port = endpoint(record, "adb", process.pid);
    try { guest_shell(process, port, "setprop sys.powerctl shutdown,userrequested", 5000); } catch (...) {
        if (owned_process_is_live(process)) qmp_powerdown(process, endpoint(record, "qmp", process.pid));
    }
    const auto deadline = GetTickCount64() + 30000;
    while (owned_process_is_live(process) && GetTickCount64() < deadline) Sleep(100);
    if (owned_process_is_live(process)) throw std::runtime_error("Graceful shutdown did not finish. Storage was retained; no process was force-killed.");
    return "Stopped instance " + id + ".\n";
}
std::string instance_logs(const ManagerPaths& paths, const std::string& id) {
    RegistryTransaction registry(paths.registryRoot, paths.channel); const auto& record = registry.instance(id);
    if (record.at("runtime").is_null() || !record.at("runtime").contains("sessionUuid")) return "No recorded startup logs.\n";
    StorageLease lease(parse_storage_identity(record.at("owner")), {paths.appRoot, paths.registryRoot});
    return utf8(session_logs(record).wstring()) + '\n';
}
std::string instance_shell(const ManagerPaths& paths, const std::string& id, const std::string& command) {
    json record;
    { RegistryTransaction registry(paths.registryRoot, paths.channel); record = registry.instance(id); }
    auto process = qemu_process(record); auto result = guest_shell(process, endpoint(record, "adb", process.pid), command, 30000);
    if (result.exitCode) throw std::runtime_error("Guest command exited with " + std::to_string(result.exitCode) + ": " + result.error + result.output);
    return result.output + result.error;
}
void supervise_instance(const ManagerPaths& paths, const std::string& id, const std::string& session) {
    json record;
    update(paths, id, session, [&](json& current) {
        bool self = false;
        for (const auto& value : current.at("runtime").at("processes")) {
            auto process = parse_owned_process(value);
            if (process.role == "supervisor" && process.pid == GetCurrentProcessId()) self = owned_process_is_live(process);
        }
        if (!self || current.at("lifecycle") != "starting") throw std::runtime_error("Unregistered desktop supervisor; refusing to launch QEMU.");
        record = current;
    });
    auto owner = parse_storage_identity(record.at("owner"));
    auto bin = fs::path(utf16(record.at("configuration").at("qemu").at("binDirectory").get<std::string>()));
    const auto log = session_logs(record);
    std::string failure;
    try {
        StorageLease lease(owner, {paths.registryRoot, paths.appRoot, bin});
        RuntimePins pins;
        for (auto relative : {L"disk", L"disk/boot", L"logs"}) pins.add(owner.directory / relative, true);
        pins.add(log, true);
        pins.add(owner.directory / "disk/phone.qcow2", true);
        pins.add(owner.directory / "disk/boot/kernel"); pins.add(owner.directory / "disk/boot/ramdisk.img");
        verify_installed_disk(record.at("configuration").at("qemu"), owner.directory / "disk", record.at("immutableSource").at("layout"), log / "disk-validation");
        PortReservation adb, qmp, camera;
        auto plan = runtime_plan(record, paths, session, adb.port(), qmp.port(), camera.port());
        if (inspect_pe(plan.camera).machine != 0xaa64) throw std::runtime_error("Missing native ARM64 camera bridge.");
        write_status(log, "WAITING", "Booting Android; waiting for its independent ADB transport");
        PrivateChild qemu(plan.qemu, plan.cwd, plan.arguments, plan.environment, log / "qemu.log");
        auto qemuOwner = describe_owned_process(qemu.pid(), plan.qemu, owner.instanceUuid, paths.channel, "qemu");
        update(paths, id, session, [&](json& current) {
            current["runtime"]["processes"].push_back(owned_process_json(qemuOwner));
            for (auto [role, port] : {std::pair{"adb", adb.port()}, {"qmp", qmp.port()}, {"camera", camera.port()}})
                current["runtime"]["endpoints"].push_back({{"role", role}, {"pid", qemu.pid()}, {"port", port}});
        });
        auto adbPort = adb.port(), cameraPort = camera.port(); adb.release(); qmp.release(); camera.release(); qemu.resume();
        PrivateChild bridge(plan.camera, log, {L"--serve", L"127.0.0.1", std::to_wstring(cameraPort), L"camera.log"}, {}, log / "camera-process.log");
        auto cameraOwner = describe_owned_process(bridge.pid(), plan.camera, owner.instanceUuid, paths.channel, "camera");
        update(paths, id, session, [&](json& current) { current["runtime"]["processes"].push_back(owned_process_json(cameraOwner)); }); bridge.resume();
        auto shell = [&](const std::string& command) {
            auto result = guest_shell(qemuOwner, adbPort, command, 5000);
            if (result.exitCode) throw std::runtime_error("Android command failed: " + command + ": " + result.error);
            return trim(result.output);
        };
        bool ready = false; auto deadline = GetTickCount64() + 240000;
        while (qemu.running() && GetTickCount64() < deadline) {
            try { if (shell("getprop sys.boot_completed") == "1") { ready = true; break; } }
            catch (const std::exception& error) { std::ofstream output(log / "setup.log", std::ios::app); output << "Waiting: " << error.what() << '\n'; }
            Sleep(500);
        }
        if (!ready) throw std::runtime_error("Android did not boot within 240 seconds. See serial.log, logcat.log and qemu.log.");
        if (shell("getprop ro.serialno") != plan.serial || shell("getprop ro.kikiaosp.build_channel") != paths.channel)
            throw std::runtime_error("Android release/development identity mismatch; desktop not exposed.");
        write_status(log, "CONFIGURING", "Android started; verifying display, power and Launcher");
        shell("dumpsys battery set -f ac 1 && dumpsys battery set -f level 100 && dumpsys battery set -f status 5 && "
              "dumpsys battery set -f temp 250 && dumpsys battery set -f present 1 && "
              "settings put global stay_on_while_plugged_in 7 && settings put secure screensaver_enabled 0 && locksettings set-disabled true");
        // Apply profile defaults once, not on every user's subsequent boot.
        if (!fs::exists(owner.directory / "display-initialized.json")) {
            shell("wm density 288 && settings put system font_scale 1.5 && settings put system min_refresh_rate 120.0 && "
                  "settings put system peak_refresh_rate 120.0");
        }
        shell("wm dismiss-keyguard && input keyevent 3");
        ready = false; deadline = GetTickCount64() + 30000;
        while (qemu.running() && GetTickCount64() < deadline) {
            auto windows = shell("dumpsys window displays"), layers = shell("dumpsys SurfaceFlinger --list"), display = shell("dumpsys display");
            if (launcher_display_ready(windows, layers, display)) { ready = true; break; }
            Sleep(500);
        }
        if (!ready || shell("dumpsys power").find("mStayOn=true") == std::string::npos || shell("locksettings get-disabled") != "true")
            throw std::runtime_error("Android booted but visible Launcher/120-Hz/stay-awake verification failed.");
        if (!fs::exists(owner.directory / "display-initialized.json")) {
            std::ofstream initialized(owner.directory / "display-initialized.json"); initialized << "{\"profileVersion\":1}\n";
            if (!initialized) throw std::runtime_error("Could not persist initial display profile.");
        }
        auto verified = "Android " + shell("getprop ro.build.version.release") + " | Linux " + shell("uname -r") + " | SDL / VirGL / 120 Hz | HOME visible";
        update(paths, id, session, [&](json& current) { current["lifecycle"] = "running"; });
        write_status(log, "READY", "System started successfully - opening Android", verified);
        while (qemu.running()) Sleep(250);
        if (qemu.exit_code()) failure = "QEMU exited with code " + std::to_string(qemu.exit_code()) + ". See qemu.log.";
        // The exact bridge and its camera lease are stopped before storage is
        // released. No host ADB daemon or other QEMU process is touched.
        bridge.terminate();
    } catch (const std::exception& error) {
        failure = error.what();
        try { write_status(log, "ERROR", failure); } catch (...) {}
    }
    update(paths, id, session, [&](json& current) {
        current["runtime"]["processes"] = json::array(); current["runtime"]["endpoints"] = json::array();
        current["lifecycle"] = "idle"; current["lastError"] = failure.empty() ? json(nullptr) : json(failure);
    });
    if (!failure.empty()) throw std::runtime_error(failure);
}
} // namespace kiki
