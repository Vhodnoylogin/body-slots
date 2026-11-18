// SPDX-License-Identifier: MIT
#pragma once
#include <RE/Skyrim.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>

namespace body_slots::game {
// Connect on the frame owner. Event threads retain POD only, never engine pointers.
// The adapter keeps this sink alive for the module's supported process lifetime.
class EquipmentHistory final : private RE::BSTEventSink<RE::TESEquipEvent> {
public:
    enum class Change { Unchanged, Changed, Unknown };
    bool Connect() {
        {std::lock_guard lock(mutex_);if(connected_)return true;}
        auto* source=RE::ScriptEventSourceHolder::GetSingleton();
        if(!source)return false;
        source->AddEventSink<RE::TESEquipEvent>(this);
        std::lock_guard lock(mutex_);connected_=true;return true;
    }
    std::uint64_t Capture() const {
        std::lock_guard lock(mutex_);return connected_&&!exhausted_.load()?revision_:0;
    }
    Change Since(std::uint32_t form,std::uint64_t captured) const {
        std::lock_guard lock(mutex_);
        if(!connected_||exhausted_.load()||!captured||captured>revision_||revision_-captured>events_.size())return Change::Unknown;
        for(auto revision=captured;revision<revision_;){
            ++revision;const auto& event=events_[revision%events_.size()];
            if(event.revision!=revision)return Change::Unknown;
            if(event.form==form)return Change::Changed;
        }
        return Change::Unchanged;
    }
private:
    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,
        RE::BSTEventSource<RE::TESEquipEvent>*) noexcept override {
        try{
            if(event&&event->actor&&event->actor->GetFormID()==0x14&&event->baseObject){
                std::lock_guard lock(mutex_);
                if(revision_==std::numeric_limits<std::uint64_t>::max())exhausted_=true;
                else {++revision_;events_[revision_%events_.size()]={revision_,event->baseObject};}
            }
        }catch(...){exhausted_=true;}
        return RE::BSEventNotifyControl::kContinue;
    }
    struct Event {std::uint64_t revision{};std::uint32_t form{};};
    mutable std::mutex mutex_;
    std::array<Event,256> events_{};
    std::uint64_t revision_=1;
    bool connected_=false;
    std::atomic<bool> exhausted_{false};
};
}
