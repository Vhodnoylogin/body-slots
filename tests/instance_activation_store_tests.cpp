#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
// Forced-boundary access to the actual store only; no production test ABI.
#define private public
#include "instances.h"
#undef private
using namespace body_slots;
using namespace body_slots::detail;
namespace {
unsigned checks=0;
#define CHECK(value) do{++checks;if(!(value))throw std::runtime_error("activation store line "+std::to_string(__LINE__));}while(false)
constexpr auto Max=std::numeric_limits<std::uint64_t>::max();
void Seed(InstanceStore& store) {
    store.state_={};store.state_.count=2;store.state_.nextRevision=3;
    for(unsigned i=0;i<2;++i){auto& e=store.state_.entries[i];e.owner=1;e.handle=i+1;e.revision=i+1;
        std::strcpy(e.provider,"owner");std::strcpy(e.spec.equipmentKey,i?"second":"first");std::strcpy(e.spec.slotKey,"opening");}
}
}
int main(){try{
    auto store=std::make_unique<InstanceStore>();Seed(*store);auto candidate=std::make_unique<InstanceStore::Candidate>();
    std::array<ActivationChange,2> changes{{{24,kInstanceActive,1,1},{24,kInstanceActive,2,2}}};
    const auto run=[&]{return store->PrepareActivation(1,changes.data(),2,1,store->ViewRevision(),*candidate);};
    auto before=std::make_unique<InstanceStore::State>(store->state_);
    CHECK(run()==Status::Ok);CHECK(!std::memcmp(&store->state_,before.get(),sizeof(*before))); // Preparation is pure.
    store->Apply(*candidate);CHECK(store->ViewRevision()==2&&store->At(0).revision==3&&store->At(1).revision==4&&store->state_.nextRevision==5);
    CHECK(store->At(0).handle==1&&store->At(1).handle==2&&store->state_.nextHandle==before->nextHandle);
    for(unsigned boundary=0;boundary<3;++boundary){Seed(*store);
        if(boundary==0)store->state_.view=Max;
        if(boundary==1)store->state_.nextRevision=Max;
        if(boundary==2)store->state_.nextRevision=Max-1; // Two changes require two available revision values.
        *before=store->state_;CHECK(run()==Status::LimitExceeded);CHECK(!std::memcmp(&store->state_,before.get(),sizeof(*before)));}
    Seed(*store);store->state_.nextRevision=Max-2;CHECK(run()==Status::Ok);store->Apply(*candidate);
    CHECK(store->At(0).revision==Max-2&&store->At(1).revision==Max-1&&store->state_.nextRevision==Max);
    changes[0].expectedInstanceRevision=Max-2;changes[1].expectedInstanceRevision=Max-1;store->state_.view=Max;*before=store->state_;
    CHECK(run()==Status::Ok);store->Apply(*candidate);CHECK(!std::memcmp(&store->state_,before.get(),sizeof(*before))); // No-op at both exhausted counters.
    Seed(*store);changes={{{24,1,1,1},{24,1,2,2}}};store->state_.entries[1].spec.flags=1;store->state_.nextRevision=Max-1;
    CHECK(run()==Status::Ok);store->Apply(*candidate);CHECK(store->At(0).revision==Max-1&&store->At(1).revision==2&&store->state_.nextRevision==Max); // Budget counts changes, not patch rows.
    Seed(*store);changes={{{24,0,1,1},{24,0,2,2}}};store->state_.view=store->state_.nextRevision=Max;
    changes[1].expectedInstanceRevision=0;CHECK(run()==Status::Conflict); // Every unchanged row still needs CAS.
    changes[1].expectedInstanceRevision=2;store->state_.entries[1].owner=2;CHECK(run()==Status::Unauthorized);
    Seed(*store);changes[1].instanceHandle=1;CHECK(run()==Status::InvalidArgument);
    changes[1].instanceHandle=2;std::strcpy(store->state_.entries[1].spec.equipmentKey,"first");CHECK(run()==Status::InvalidArgument); // Resolved duplicate key.
    Seed(*store);changes[1].structSize=0;CHECK(run()==Status::UnsupportedVersion);changes[1].structSize=24;
    changes[1].flags=2;CHECK(run()==Status::InvalidArgument);changes[1].flags=0;
    CHECK(store->PrepareActivation(1,changes.data(),2,0,1,*candidate)==Status::Conflict);
    CHECK(store->PrepareActivation(1,changes.data(),2,1,0,*candidate)==Status::Conflict);
    CHECK(ValidateActivationChanges(nullptr,1)==Status::InvalidArgument&&ValidateActivationChanges(changes.data(),0)==Status::InvalidArgument);
    CHECK(ValidateActivationChanges(changes.data(),257)==Status::InvalidArgument);
    std::cout<<"PASS activation actual-store forced boundaries "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
