#include "main/ppb_digest.h"
#include <array>
#include <cstdlib>

int main() {
    using namespace body_slots;
    std::array<PPBAPI::PpbTouchContact, 2> contacts{};
    auto& hand = contacts[1];
    hand.actorFormId = 17;
    hand.toucherFormId = 0x14;
    hand.slot = 2;
    hand.child = 3;
    hand.sourceKind = PPBAPI::kSourcePalm;
    hand.region = 7;
    hand.engineContact = 1;
    auto& other = contacts[0];
    other = hand;
    other.sourceKind = PPBAPI::kSourceHead;
    other.region = 99;
    const PpbRegionQuery q{sizeof(q), 17, Right, 2, 0, 3};
    PPBAPI::PpbTouchFrame frame{4, 12, 7, 8, 2};
    PpbRegionEvidence result{};
    if (detail::SelectPpbDigest(q, frame, contacts, result) != Status::Ok || !result.reported ||
        result.region != 7 || result.available != 8 || result.copied != 2 || !result.retainedEngineContact ||
        result.ppbSequence != 12)
        return EXIT_FAILURE;
    hand.wand = 1;
    if (detail::SelectPpbDigest(q, frame, contacts, result) != Status::Ok || result.reported)
        return EXIT_FAILURE;
    frame.copied = 3;
    if (detail::SelectPpbDigest(q, frame, contacts, result) != Status::Unavailable)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
