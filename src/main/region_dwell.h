#pragma once
#include "body_slots_region_dwell_api.h"
#include "higgshandapi.h"
#include "higgsinput.h"
#include <mutex>
#include <optional>

namespace body_slots::detail {
struct DwellProvider {
    HIGGS_SharedHandUseProposalsInterface proposals{};
    HIGGS_HandUseInterface uses{};
    HIGGS_InputContextInterface contexts{};
    HIGGS_AuthorizedDispatchInterface dispatch{};
    bool Available() const;
};
struct DwellEvidence {
    RegionDwellContext context{};
    std::uint64_t sourceRevision{};
};
using ReadDwellEvidence = bool (*)(void*, const HIGGS_HandSnapshot&, DwellEvidence&);

bool ValidDwellSpec(const RegionDwellBindingSpec&);
class DwellVisit {
public:
    DwellVisit(RegionDwellBindingSpec, DwellProvider, std::uint64_t targetKey);
    HIGGS_HandStatus EnableSharedRounds() const;
    void Tick(void* binding, HIGGS_HandExecutor, const HIGGS_HandSnapshot&, ReadDwellEvidence);
    void Invalidate(bool removed);

private:
    struct Attempt {
        DwellEvidence start;
        std::uint64_t menuGeneration{}, proposal{};
    };
    struct Invocation {
        DwellVisit& visit;
        void* binding;
        ReadDwellEvidence read;
        Attempt attempt;
        HIGGS_HandExecutor executor;
        bool entered{};
    };
    static HIGGS_HandStatus HIGGS_HAND_CALL Execute(const HIGGS_ActionRequest*, const HIGGS_HandSnapshot*, void*) noexcept;
    bool Eligible(const RegionDwellContext&) const;
    std::uint64_t Gameplay(std::uint64_t epoch) const;
    void Retire(const Attempt&);
    void Finish(Invocation&, const HIGGS_ActionRequest&);
    bool Claim(std::uint64_t visit);
    RegionDwellBindingSpec spec_;
    DwellProvider provider_;
    const std::uint64_t targetKey_;
    std::mutex mutex_;
    std::uint64_t visit_{};
    std::optional<Attempt> pending_;
    bool armed_{true}, stopped_{};
};
}
