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
#include "configuration.h"
#include "instances.h"
#include "instance_configuration.h"
#include "runtime.h"
#include "runtime_saved.h"
#if defined(BODY_SLOTS_WITH_GAME_ADAPTER)
#include "game_adapter.h"
#endif
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
constexpr UInt32 kSerializationId = 0x534C5342; // BSLS
constexpr UInt32 kConfigurationRecord = 0x46435342; // BSCF
constexpr UInt32 kInstanceConfigurationRecord = 0x49435342; // BSCI
constexpr UInt32 kRuntimeConfigurationRecord = 0x54525342; // BSRT
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
bool BeginSaveScope(body_slots::InstanceView* committed = nullptr) noexcept {
#if defined(BODY_SLOTS_WITH_GAME_ADAPTER)
    body_slots::game::Reset();
#endif
    body_slots::detail::InvalidateRuntime();
    const auto status = body_slots::detail::ResetSaveScope(committed);
    if (status != body_slots::Status::Ok) Log(State().packages, "save scope reset failed");
    return status == body_slots::Status::Ok;
}
void OnRevert(SKSESerializationInterface*) noexcept { BeginSaveScope(); }
void OnSave(SKSESerializationInterface* serialization) noexcept {
    try {
        if (!serialization || !serialization->WriteRecord) return;
        std::vector<std::uint8_t> definitions, instances, runtime; bool hasInstances = false, hasRuntime = false;
        const auto status = body_slots::detail::SaveConfigurationBatch(definitions, instances, hasInstances, runtime, hasRuntime);
        if (status != body_slots::Status::Ok) { Log(State().packages, "customization batch encoding failed"); return; }
        // BSCI requires its BSCF partner on load. Writing it first makes a failed
        // second write reject the incomplete batch; already-written IO is not rolled back.
        if (hasRuntime && !serialization->WriteRecord(kRuntimeConfigurationRecord,
            body_slots::detail::kRuntimeCustomizationSaveVersion, runtime.data(), static_cast<UInt32>(runtime.size()))) {
            Log(State().packages, "runtime customization save failed"); return;
        }
        if (hasInstances && !serialization->WriteRecord(kInstanceConfigurationRecord,
            body_slots::detail::kInstanceCustomizationSaveVersion, instances.data(), static_cast<UInt32>(instances.size()))) {
            Log(State().packages, "instance customization save failed"); return;
        }
        if (!serialization->WriteRecord(kConfigurationRecord, body_slots::detail::kCustomizationSaveVersion,
            definitions.data(), static_cast<UInt32>(definitions.size()))) Log(State().packages, "definition customization save failed");
    } catch (...) { Log(State().packages, "customization save internal error"); }
}
void OnLoad(SKSESerializationInterface* serialization) noexcept {
    // End the previous save's state even when the next cosave is empty or malformed.
    // All records are staged before the single validated registry commit below.
    body_slots::InstanceView loadView{};
    if (!BeginSaveScope(&loadView)) return;
    try {
        if (!serialization || !serialization->GetNextRecordInfo || !serialization->ReadRecordData) return;
        std::vector<std::uint8_t> definitions, instances, runtime; bool seenDefinitions = false, seenInstances = false, seenRuntime = false;
        UInt32 type = 0, version = 0, length = 0;
        while (serialization->GetNextRecordInfo(&type, &version, &length)) {
            std::vector<std::uint8_t>* candidate = nullptr;
            if (type == kConfigurationRecord && !seenDefinitions && version == body_slots::detail::kCustomizationSaveVersion
                && length >= body_slots::detail::kCustomizationSaveHeaderBytes && length <= body_slots::detail::kMaxCustomizationSaveBytes) {
                seenDefinitions = true; candidate = &definitions;
            } else if (type == kInstanceConfigurationRecord && !seenInstances && version == body_slots::detail::kInstanceCustomizationSaveVersion
                && length >= body_slots::detail::kInstanceCustomizationSaveHeaderBytes && length <= body_slots::detail::kMaxInstanceCustomizationSaveBytes) {
                seenInstances = true; candidate = &instances;
            } else if (type == kRuntimeConfigurationRecord && !seenRuntime && (version >= 1 && version <= body_slots::detail::kRuntimeCustomizationSaveVersion)
                && length >= 16 && length <= body_slots::detail::kMaxRuntimeCustomizationSaveBytes) {
                seenRuntime = true; candidate = &runtime;
            } else { Log(State().packages, "customization load rejected duplicate/unknown/unbounded record batch"); return; }
            candidate->resize(length);
            if (serialization->ReadRecordData(candidate->data(), length) != length) {
                Log(State().packages, "customization load rejected truncated record"); return;
            }
        }
        if ((seenInstances || seenRuntime) && !seenDefinitions) { Log(State().packages, "customization load missing BSCF partner"); return; }
        if (seenRuntime) {
            const auto resolver = [](std::uint32_t oldId, std::uint32_t* out, void* context) -> bool {
                auto* api = static_cast<SKSESerializationInterface*>(context);
                UInt32 resolved = 0;
                if (!api->ResolveFormId || !api->ResolveFormId(oldId, &resolved)) return false;
                *out = static_cast<std::uint32_t>(resolved); return true;
            };
            if (body_slots::detail::RemapRuntimeCustomizationForms(runtime, resolver, serialization) != body_slots::Status::Ok) {
                Log(State().packages, "runtime customization form remap failed; no batch applied"); return;
            }
        }
        if (seenDefinitions && body_slots::detail::LoadConfigurationBatch(&definitions, seenInstances ? &instances : nullptr, seenRuntime ? &runtime : nullptr,
            loadView.saveGeneration, loadView.viewRevision) != body_slots::Status::Ok)
            Log(State().packages, "customization load rejected invalid/stale batch; no prefix was applied");
    } catch (...) { Log(State().packages, "customization load internal error; author defaults remain effective"); }
}
void OnMessage(SKSEMessagingInterface::Message* message) noexcept {
    try {
        if (!message || !message->sender || std::strcmp(message->sender, "SKSE")) return;
#if defined(BODY_SLOTS_WITH_GAME_ADAPTER)
        if (message->type == SKSEMessagingInterface::kMessage_PostPostLoad) {
            body_slots::game::Connect(); return;
        }
        if (message->type == SKSEMessagingInterface::kMessage_PostLoadGame) {
            // SKSE VR passes the boolean value in the pointer itself, with length one.
            body_slots::game::SetWorldPlayable(message->dataLen == 1 && reinterpret_cast<std::uintptr_t>(message->data) == 1);
            return;
        }
#endif
        if (message->type == SKSEMessagingInterface::kMessage_PreLoadGame || message->type == SKSEMessagingInterface::kMessage_NewGame) {
            if (!BeginSaveScope()) return;
#if defined(BODY_SLOTS_WITH_GAME_ADAPTER)
            if (message->type == SKSEMessagingInterface::kMessage_NewGame)
                body_slots::game::SetWorldPlayable(true);
#endif
            return;
        }
        if (message->type != SKSEMessagingInterface::kMessage_PostLoad) return;
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
    info->infoVersion = PluginInfo::kInfoVersion; info->name = kPluginName; info->version = 3;
    try { return Compatible(skse); } catch (...) { return false; }
}
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* skse) noexcept {
    try {
        if (!Compatible(skse) || !skse->QueryInterface || !skse->GetPluginHandle) return false;
        const auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
        if (!messaging || messaging->interfaceVersion < SKSEMessagingInterface::kInterfaceVersion || !messaging->RegisterListener) return false;
        const auto* serialization = static_cast<SKSESerializationInterface*>(skse->QueryInterface(kInterface_Serialization));
        if (!serialization || serialization->version < SKSESerializationInterface::kVersion || !serialization->SetUniqueID ||
            !serialization->SetRevertCallback || !serialization->SetSaveCallback || !serialization->SetLoadCallback ||
            !serialization->WriteRecord || !serialization->GetNextRecordInfo || !serialization->ReadRecordData) return false;
        auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
        if (state.loaded) return false;
        const auto handle = skse->GetPluginHandle(); if (handle == kPluginHandle_Invalid) return false;
        const auto directory = Host().parent_path() / L"Data" / L"SKSE" / L"Plugins" / L"BodySlots";
        if (!messaging->RegisterListener(handle, "SKSE", OnMessage)) return false;
        serialization->SetUniqueID(handle, kSerializationId);
        serialization->SetRevertCallback(handle, OnRevert);
        serialization->SetSaveCallback(handle, OnSave);
        serialization->SetLoadCallback(handle, OnLoad);
        state.handle = handle; state.packages = directory; state.loaded = true;
#if defined(BODY_SLOTS_WITH_GAME_ADAPTER)
        body_slots::game::Initialize(skse);
#endif
        return true;
    } catch (...) { return false; }
}
