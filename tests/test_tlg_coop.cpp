// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include "input/tlg_coop.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)

using namespace Input::TlgCoop;
using Pads = std::array<PadInput, kPhysicalPads>;
using Kind = Event::Kind;

namespace {

Pads Connected(int count) {
    Pads pads{};
    for (int i = 0; i < count; ++i) {
        pads[i].connected = true;
    }
    return pads;
}

// Holds L3+R3 on one controller from `start` until the swap fires; returns the events.
std::vector<Event> HoldSwap(Session& s, Pads pads, int c, u64& now) {
    std::vector<Event> all;
    pads[c].buttons |= kButtonL3 | kButtonR3;
    for (int frame = 0; frame < 12; ++frame) {
        now += 33'333;
        for (const auto& e : s.Update(now, pads)) {
            all.push_back(e);
        }
    }
    pads[c].buttons &= ~(kButtonL3 | kButtonR3);
    now += 33'333;
    for (const auto& e : s.Update(now, pads)) {
        all.push_back(e);
    }
    return all;
}

void SoloStartsAsBoyAndSwapsToTrico() {
    Session s;
    u64 now = 0;
    const Pads pads = Connected(1);
    CHECK(s.Update(now, pads).empty());
    CHECK(s.ControllerFor(Role::Boy) == 0);
    CHECK(s.ControllerFor(Role::Trico) == kNoController);
    CHECK(s.Flags() == (kFlagActive | kFlagBoyHuman | (kProtocolVersion << kVersionShift)));

    auto events = HoldSwap(s, pads, 0, now);
    CHECK(events.size() == 1);
    CHECK((events[0] == Event{Kind::Swapped, 0, Role::Boy}));
    CHECK(s.ControllerFor(Role::Boy) == kNoController);
    CHECK(s.ControllerFor(Role::Trico) == 0);
    CHECK(s.Sequence() == 1);
    CHECK((s.Flags() & kFlagTricoHuman) && !(s.Flags() & kFlagBoyHuman));
    CHECK(!(s.Flags() & kFlagTricoStartedSwap));

    events = HoldSwap(s, pads, 0, now);
    CHECK((events.size() == 1 && events[0] == Event{Kind::Swapped, 0, Role::Trico}));
    CHECK(s.ControllerFor(Role::Boy) == 0);
    CHECK(s.Sequence() == 2);
    CHECK(s.Flags() & kFlagTricoStartedSwap);
}

void SwapNeedsThreeTenthsOfASecondAndFiresOnce() {
    Session s;
    Pads pads = Connected(1);
    pads[0].buttons = kButtonL3 | kButtonR3;
    CHECK(s.Update(0, pads).empty());
    CHECK(s.MaskFor(0) == (kButtonL3 | kButtonR3)); // the game never sees the combo
    CHECK(s.Update(kSwapHoldMicros - 1, pads).empty());
    CHECK(s.Update(kSwapHoldMicros, pads).size() == 1);
    CHECK(s.Update(kSwapHoldMicros * 5, pads).empty()); // still held: no second swap
    // releasing one stick keeps the other masked until it is released too
    pads[0].buttons = kButtonR3;
    CHECK(s.Update(kSwapHoldMicros * 6, pads).empty());
    CHECK(s.MaskFor(0) == kButtonR3);
    pads[0].buttons = 0;
    s.Update(kSwapHoldMicros * 7, pads);
    CHECK(s.MaskFor(0) == 0);
    // a single stick click reaches the game
    pads[0].buttons = kButtonL3;
    s.Update(kSwapHoldMicros * 8, pads);
    CHECK(s.MaskFor(0) == 0);
    // a short combo does nothing
    pads[0].buttons = kButtonL3 | kButtonR3;
    s.Update(kSwapHoldMicros * 9, pads);
    pads[0].buttons = 0;
    CHECK(s.Update(kSwapHoldMicros * 9 + 100'000, pads).empty());
    CHECK(s.Sequence() == 1);
}

void SecondPlayerJoinsWithOptionsAndTakesTrico() {
    Session s;
    u64 now = 0;
    Pads pads = Connected(2);
    pads[1].buttons = 0;
    s.Update(now, pads);
    CHECK(s.ControllerFor(Role::Trico) == kNoController); // connected is not joined
    pads[1].buttons = kButtonOptions;
    const auto events = s.Update(now += 33'333, pads);
    CHECK((events.size() == 1 && events[0] == Event{Kind::Joined, 1, Role::Trico}));
    CHECK(s.ControllerFor(Role::Trico) == 1);
    CHECK(s.MaskFor(1) == kButtonOptions); // the join press does not pause the game
    CHECK(s.Flags() & kFlagTwoPlayers);
    pads[1].buttons = 0;
    s.Update(now += 33'333, pads);
    CHECK(s.MaskFor(1) == 0);
    // Options from a joined player reaches the game
    pads[1].buttons = kButtonOptions;
    CHECK(s.Update(now += 33'333, pads).empty());
    CHECK(s.MaskFor(1) == 0);

    // either player can start the swap; roles exchange
    pads[1].buttons = 0;
    s.Update(now += 33'333, pads);
    auto swap = HoldSwap(s, pads, 1, now);
    CHECK((swap.size() == 1 && swap[0] == Event{Kind::Swapped, 1, Role::Trico}));
    CHECK(s.ControllerFor(Role::Boy) == 1 && s.ControllerFor(Role::Trico) == 0);
    CHECK(s.Flags() & kFlagTricoStartedSwap);
    swap = HoldSwap(s, pads, 1, now);
    CHECK((swap.size() == 1 && swap[0] == Event{Kind::Swapped, 1, Role::Boy}));
    CHECK(s.ControllerFor(Role::Boy) == 0 && s.ControllerFor(Role::Trico) == 1);
    CHECK(!(s.Flags() & kFlagTricoStartedSwap));
}

void JoinTakesTheBoyWhenTheSoloPlayerIsTrico() {
    Session s;
    u64 now = 0;
    Pads pads = Connected(3);
    HoldSwap(s, pads, 0, now);
    CHECK(s.ControllerFor(Role::Trico) == 0);
    pads[2].buttons = kButtonOptions;
    const auto events = s.Update(now += 33'333, pads);
    CHECK((events.size() == 1 && events[0] == Event{Kind::Joined, 2, Role::Boy}));
    // a third controller cannot join when both roles are taken
    pads[1].buttons = kButtonOptions;
    CHECK(s.Update(now += 33'333, pads).empty());
    CHECK(s.MaskFor(1) == 0);
}

void DisconnectFreesTheRole() {
    Session s;
    u64 now = 0;
    Pads pads = Connected(2);
    pads[1].buttons = kButtonOptions;
    s.Update(now, pads);
    CHECK(s.ControllerFor(Role::Trico) == 1);
    pads[1] = {};
    auto events = s.Update(now += 33'333, pads);
    CHECK((events.size() == 1 && events[0] == Event{Kind::Left, 1, Role::Trico}));
    CHECK(s.ControllerFor(Role::Trico) == kNoController);
    CHECK(!(s.Flags() & kFlagTwoPlayers));

    // when nobody is left, controller 0 plays the boy again as soon as it is back
    pads[0] = {};
    events = s.Update(now += 33'333, pads);
    CHECK((events.size() == 1 && events[0] == Event{Kind::Left, 0, Role::Boy}));
    CHECK(!s.AnyHuman());
    pads[0].connected = true;
    events = s.Update(now += 33'333, pads);
    CHECK((events.size() == 1 && events[0] == Event{Kind::Joined, 0, Role::Boy}));
}

void SelectTogglesTheCamera() {
    Session s;
    u64 now = 0;
    Pads pads = Connected(2);
    pads[1].buttons = kButtonTouchPad; // not joined: ignored
    CHECK(s.Update(now, pads).empty());
    CHECK(!s.TricoCamera());
    pads[0].buttons = kButtonTouchPad;
    auto events = s.Update(now += 33'333, pads);
    CHECK((events.size() == 1 && events[0] == Event{Kind::CameraToggled, 0, Role::Boy}));
    CHECK(s.TricoCamera() && (s.Flags() & kFlagTricoCamera));
    CHECK(s.MaskFor(0) == kButtonTouchPad);
    CHECK(s.Update(now += 33'333, pads).empty()); // held: toggles once
    pads[0].buttons = 0;
    s.Update(now += 33'333, pads);
    pads[0].buttons = kButtonTouchPad;
    s.Update(now += 33'333, pads);
    CHECK(!s.TricoCamera());
}

void SecondPadPatchVerifiesBytes() {
    std::vector<u8> image(kSecondPadPatchOffset + 16, 0x90);
    CHECK(ApplySecondPadPatch(image.data(), image.size()) == PatchResult::Mismatch);
    for (std::size_t i = 0; i < kSecondPadOriginal.size(); ++i) {
        image[kSecondPadPatchOffset + i] = kSecondPadOriginal[i];
    }
    CHECK(ApplySecondPadPatch(image.data(), 100) == PatchResult::OutOfRange);
    CHECK(ApplySecondPadPatch(nullptr, image.size()) == PatchResult::OutOfRange);
    CHECK(ApplySecondPadPatch(image.data(), image.size()) == PatchResult::Applied);
    // user id 1001, little endian, in the instruction's immediate
    CHECK(image[kSecondPadPatchOffset + 3] == 0xe9 && image[kSecondPadPatchOffset + 4] == 0x03);
    CHECK(image[kSecondPadPatchOffset + 7] == 0x90);
    CHECK(ApplySecondPadPatch(image.data(), image.size()) == PatchResult::AlreadyApplied);
    CHECK(IsGuardianSerial("CUSA03745") && !IsGuardianSerial("CUSA00001"));
}

void ScriptsHelloIsNotText() {
    // the scripts' debug text uses printable ASCII and newlines only
    CHECK(IsScriptsHello(0xC0, 0x0F, 0xEE));
    CHECK(!IsScriptsHello('C', 'O', 'O'));
    for (int c : kScriptsHelloColour) {
        CHECK(c > 0x7E || c < 0x20);
    }
    setenv("SHADPS4_TLG_COOP", "1", 1);
    CHECK(EnabledByEnvironment());
    setenv("SHADPS4_TLG_COOP", "10", 1);
    CHECK(!EnabledByEnvironment());
    unsetenv("SHADPS4_TLG_COOP");
    CHECK(!EnabledByEnvironment());
}

void TricoInputPacking() {
    CHECK(PackStick(kStickCentre, kStickCentre) == 0x8080);
    CHECK(PackStick(0xFF, 0x00) == 0x00FF); // full right, full up
    CHECK(ScriptButtons(0) == 0);
    CHECK(ScriptButtons(0x4000) == 1);             // Cross
    CHECK(ScriptButtons(0x0200) == 0x80);          // R2
    CHECK(ScriptButtons(0x4000 | 0x0200 | kButtonL3 | kButtonOptions) == 0x81);
    CHECK(ScriptButtons(0x0010) == 0x100);         // Up
    CHECK(ScriptButtons(0x0040) == 0x200);         // Down
    CHECK(ScriptButtons(0x0080) == 0x400);         // Left
    CHECK(ScriptButtons(0x0020) == 0x800);         // Right
    CHECK(ScriptButtons(0xFFFF) == 0xFFF);         // all twelve, nothing else (L3, R3, Options...)
    CHECK(StatePointY(0x1213, 0x2000) == 0x0023); // sequence low 4 bits, Circle
    CHECK(StatePointY(0x000F, 0x0020) == 0x800F); // Right in the top bit
    CHECK(kProtocolVersion == 4);
    CHECK(kTouchStickId != kTouchMagicId);
}

void ShortPressesSurviveUntilRead() {
    PressLatch latch;
    constexpr u32 cross = 0x4000, circle = 0x2000;
    // a tap that starts and ends between two reads: only the queue saw it
    CHECK(latch.Read(1'000'000, cross, 0) == cross);
    // still reported until 150 ms after it was first seen
    CHECK(latch.Read(1'060'000, 0, 0) == cross);
    CHECK(latch.Read(1'120'000, 0, 0) == cross);
    CHECK(latch.Read(1'160'000, 0, 0) == 0);
    // a press held across reads is reported as long as it is held, then released at once
    CHECK(latch.Read(2'000'000, circle, circle) == circle);
    CHECK(latch.Read(2'500'000, circle, circle) == circle);
    CHECK(latch.Read(2'560'000, 0, 0) == 0);
    // a long gap between reads: the tap is still reported once
    CHECK(latch.Read(3'000'000, cross, 0) == cross);
    CHECK(latch.Read(3'400'000, 0, 0) == 0);
    // two buttons, each on its own clock
    CHECK(latch.Read(4'000'000, cross, 0) == cross);
    CHECK(latch.Read(4'100'000, circle, 0) == (cross | circle));
    CHECK(latch.Read(4'160'000, 0, 0) == circle);
    CHECK(latch.Read(4'260'000, 0, 0) == 0);
    // a press that arrives in the newest state alone (no queue) counts too
    CHECK(latch.Read(5'000'000, 0, cross) == cross);
    CHECK(latch.Read(5'050'000, 0, 0) == cross);
    latch.Reset();
    CHECK(latch.Read(5'060'000, 0, 0) == 0);
}

void LoneL3TapTurnsTrico() {
    Session s;
    const auto turns = [&s] { return (s.Flags() >> kTurnShift) & 0x3; };
    u64 now = 1'000'000;
    CHECK(turns() == 0);
    // a tap: pressed and let go within 0.6 s (the buttons as the latch hands them on)
    s.NoteTricoButtons(now += 60'000, kButtonL3);
    s.NoteTricoButtons(now += 60'000, kButtonL3);
    s.NoteTricoButtons(now += 60'000, 0);
    CHECK(turns() == 1 && s.TurnCount() == 1);
    // a click shorter than a read, through the latch: still one tap
    PressLatch latch;
    now += 60'000;
    s.NoteTricoButtons(now, latch.Read(now, kButtonL3, 0));
    for (int i = 0; i < 4; ++i) {
        now += 60'000;
        s.NoteTricoButtons(now, latch.Read(now, 0, 0));
    }
    CHECK(s.TurnCount() == 2);
    // held too long (over 0.6 s): no turn
    for (int i = 0; i < 12; ++i) {
        s.NoteTricoButtons(now += 60'000, kButtonL3);
    }
    s.NoteTricoButtons(now += 60'000, 0);
    CHECK(s.TurnCount() == 2);
    // L3, then R3 with it: not a turn
    s.NoteTricoButtons(now += 60'000, kButtonL3);
    s.NoteTricoButtons(now += 60'000, kButtonL3 | kButtonR3);
    s.NoteTricoButtons(now += 60'000, 0);
    CHECK(s.TurnCount() == 2);
    // the count wraps in its two bits
    for (int t = 0; t < 3; ++t) {
        s.NoteTricoButtons(now += 60'000, kButtonL3);
        s.NoteTricoButtons(now += 60'000, 0);
    }
    CHECK(s.TurnCount() == 5 && turns() == 1);
    CHECK((s.Flags() >> kVersionShift) == kProtocolVersion);
    // a swap forgets a half-made tap
    Pads pads = Connected(1);
    s.Update(now += 33'333, pads);
    s.NoteTricoButtons(now += 60'000, kButtonL3);
    HoldSwap(s, pads, 0, now);
    s.NoteTricoButtons(now += 60'000, 0);
    CHECK(s.TurnCount() == 5);
}

} // namespace

int main() {
    SoloStartsAsBoyAndSwapsToTrico();
    SwapNeedsThreeTenthsOfASecondAndFiresOnce();
    SecondPlayerJoinsWithOptionsAndTakesTrico();
    JoinTakesTheBoyWhenTheSoloPlayerIsTrico();
    DisconnectFreesTheRole();
    SelectTogglesTheCamera();
    SecondPadPatchVerifiesBytes();
    ScriptsHelloIsNotText();
    TricoInputPacking();
    ShortPressesSurviveUntilRead();
    LoneL3TapTurnsTrico();
    std::puts("tlg_coop: ok");
    return 0;
}
