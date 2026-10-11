// Only the public SDK headers are input. No VRIK/private engine source or hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include "common/ITypes.h"
#include "skse64/PluginAPI.h"
#include "skse64_common/skse_version.h"
#include "package.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <memory>
#include <vector>

extern "C" const body_slots::Api* BS_CALL BodySlots_QueryInterface(std::uint32_t, std::uint32_t) noexcept;
extern "C" __declspec(dllexport) body_slots::Status BS_CALL BodySlots_LoadPackage(const wchar_t* path, std::uint64_t* client) noexcept {
    try {
        if (!path || !client || !path[0]) return body_slots::Status::InvalidArgument;
        auto package = std::make_unique<body_slots::detail::Package>(); std::string diagnostic;
        const auto status = body_slots::detail::ReadPackage(std::filesystem::path(path), *package, diagnostic);
        if (status != body_slots::Status::Ok) return status;
        const auto* api = BodySlots_QueryInterface(body_slots::kApiVersion, sizeof(body_slots::Api));
        return api ? body_slots::detail::PublishPackage(*api, *package, client) : body_slots::Status::Unavailable;
    } catch (...) { return body_slots::Status::InternalError; }
}
namespace {
constexpr const char* kPluginName = "BodySlots";
struct PluginState {
    std::mutex mutex;
    bool loaded = false, packagesLoaded = false;
    PluginHandle handle = static_cast<PluginHandle>(kPluginHandle_Invalid);
    std::filesystem::path packages;
    std::array<std::uint64_t, 64> packageTokens{};
    std::uint32_t packageCount = 0;
};
PluginState& State() { static PluginState state; return state; }
std::filesystem::path Host() {
    std::array<wchar_t, 32768> buffer{};
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::filesystem::path(buffer.data());
}
bool Compatible(const SKSEInterface* skse) {
    return skse && !skse->isEditor && skse->runtimeVersion == RUNTIME_VR_VERSION_1_4_15 &&
        skse->skseVersion >= PACKED_SKSE_VERSION && _wcsicmp(Host().filename().c_str(), L"SkyrimVR.exe") == 0;
}
void Log(const std::filesystem::path& directory, const std::string& message) noexcept {
    try {
        OutputDebugStringA(("BodySlots: " + message + "\n").c_str());
        // Do not create directories or touch Skyrim files before the PostLoad phase.
        std::ofstream stream(directory / "BodySlots.log", std::ios::app);
        if (stream) stream << message << '\n';
    } catch (...) {}
}
void OnMessage(SKSEMessagingInterface::Message* message) noexcept {
    try {
        if (!message || !message->sender || std::strcmp(message->sender, "SKSE") ||
            message->type != SKSEMessagingInterface::kMessage_PostLoad) return;
        auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.loaded || state.packagesLoaded) return;
        state.packagesLoaded = true;
        const auto* api = BodySlots_QueryInterface(body_slots::kApiVersion, sizeof(body_slots::Api));
        if (!api) { Log(state.packages, "registry interface unavailable"); return; }
        std::error_code ec;
        if (!std::filesystem::is_directory(state.packages, ec)) {
            Log(state.packages, "package directory unavailable; no authored definitions loaded"); return;
        }
        std::vector<std::filesystem::path> paths;
        // Bound scanning as well as provider/file sizes. Extra packages remain unavailable.
        for (std::filesystem::directory_iterator it(state.packages, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == L".bslots") {
                if (paths.size() == 64) { Log(state.packages, "package-file limit exceeded; refusing directory batch"); return; }
                paths.push_back(it->path());
            }
        }
        if (ec) { Log(state.packages, "cannot enumerate packages; refusing directory batch"); return; }
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            auto package = std::make_unique<body_slots::detail::Package>(); std::string diagnostic;
            auto status = body_slots::detail::ReadPackage(path, *package, diagnostic);
            std::uint64_t token = 0;
            if (status == body_slots::Status::Ok) status = body_slots::detail::PublishPackage(*api, *package, &token);
            if (status == body_slots::Status::Ok) {
                state.packageTokens[state.packageCount++] = token;
                Log(state.packages, path.filename().string() + ": published " + std::to_string(package->count) + " definitions");
            } else Log(state.packages, path.filename().string() + ": unavailable status=" +
                std::to_string(static_cast<std::uint32_t>(status)) + " " + diagnostic);
        }
    } catch (...) {
        try { Log(State().packages, "package load failed with internal error"); } catch (...) {}
    }
}
} // namespace

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info) noexcept {
    if (!info) return false;
    info->infoVersion = PluginInfo::kInfoVersion; info->name = kPluginName; info->version = 1;
    try { return Compatible(skse); } catch (...) { return false; }
}
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* skse) noexcept {
    try {
        if (!Compatible(skse) || !skse->QueryInterface || !skse->GetPluginHandle) return false;
        const auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
        if (!messaging || messaging->interfaceVersion < SKSEMessagingInterface::kInterfaceVersion || !messaging->RegisterListener) return false;
        auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
        if (state.loaded) return false;
        const auto handle = skse->GetPluginHandle(); if (handle == kPluginHandle_Invalid) return false;
        const auto directory = Host().parent_path() / L"Data" / L"SKSE" / L"Plugins" / L"BodySlots";
        if (!messaging->RegisterListener(handle, "SKSE", OnMessage)) return false;
        state.handle = handle; state.packages = directory; state.loaded = true;
        return true;
    } catch (...) { return false; }
}
