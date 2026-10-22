// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

// The Last Guardian co-op (SHADPS4_TLG_COOP=1): which physical controller plays the boy (the
// game's first pad) and which plays Trico (the game's second pad, read by the co-op scripts).
//
// - Solo: controller 0 plays the boy. L3+R3 held for 0.3 s moves the player to Trico and back.
// - A second player joins by pressing Options on any other connected controller and takes the
//   free role. With two players, L3+R3 swaps the roles.
// - Select (the touchpad button: Share/Back is bound to it by default) switches the camera
//   between boy framing and Trico framing.
// - L3 tapped alone by the Trico player (let go within 0.6 s, R3 never pressed) asks Trico to
//   turn: a 2-bit count in the flags, so the scripts see every tap.
// - The game never sees the presses that drive these actions.
//
// The state reaches the scripts as the second touch point of the game's second pad
// (see TouchFlags), so no game code reads emulator memory.
namespace Input::TlgCoop {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// OrbisPadButtonDataOffset bits (checked against pad.h where the module is wired in).
inline constexpr u32 kButtonL3 = 0x00000002;
inline constexpr u32 kButtonR3 = 0x00000004;
inline constexpr u32 kButtonOptions = 0x00000008;
inline constexpr u32 kButtonTouchPad = 0x00100000;

inline constexpr int kNoController = -1;
inline constexpr int kPhysicalPads = 4;
inline constexpr u64 kSwapHoldMicros = 300'000;
inline constexpr u64 kTurnTapMicros = 600'000; // a thumb click mid-push can run long

// Second touch point of the game's second pad: id marks it, x carries the flags, y the swap count.
inline constexpr u8 kTouchMagicId = 0xC0;
inline constexpr u16 kProtocolVersion = 4;
inline constexpr u16 kFlagActive = 1u << 0;
inline constexpr u16 kFlagBoyHuman = 1u << 1;
inline constexpr u16 kFlagTricoHuman = 1u << 2;
inline constexpr u16 kFlagTricoCamera = 1u << 3;
inline constexpr u16 kFlagTricoStartedSwap = 1u << 4;
inline constexpr u16 kFlagTwoPlayers = 1u << 5;
inline constexpr int kTurnShift = 6; // 2 bits: the Trico player's L3 taps, counted mod 4
inline constexpr int kVersionShift = 12;

enum class Role : u8 { Boy = 0, Trico = 1 };

inline constexpr Role Other(Role role) {
    return role == Role::Boy ? Role::Trico : Role::Boy;
}

struct PadInput {
    bool connected = false;
    u32 buttons = 0;
};

struct Event {
    enum class Kind : u8 { Joined, Left, Swapped, CameraToggled };
    Kind kind;
    int controller; // the controller that caused it
    Role role;      // Joined/Left: the role; Swapped: the initiator's role before the swap
    bool operator==(const Event&) const = default;
};

class Session {
public:
    Session() {
        roles_ = {0, kNoController};
    }

    /// Feeds the newest state of every physical controller (call once per game frame).
    std::vector<Event> Update(u64 now_micros, const std::array<PadInput, kPhysicalPads>& pads) {
        std::vector<Event> events;
        for (int r = 0; r < 2; ++r) {
            const int c = roles_[r];
            if (c != kNoController && !pads[c].connected) {
                roles_[r] = kNoController;
                ResetController(c);
                events.push_back({Event::Kind::Left, c, static_cast<Role>(r)});
            }
        }
        if (!AnyHuman() && pads[0].connected) {
            roles_[0] = 0;
            events.push_back({Event::Kind::Joined, 0, Role::Boy});
        }

        for (int c = 0; c < kPhysicalPads; ++c) {
            const PadInput& pad = pads[c];
            const u32 held = pad.connected ? pad.buttons : 0;
            const u32 pressed = held & ~previous_[c];
            previous_[c] = held;
            if (!pad.connected) {
                ResetController(c);
                continue;
            }
            // Latched masks end once their buttons are released.
            mask_[c] &= held;

            const int role = RoleOf(c);
            if (role < 0) {
                if (pressed & kButtonOptions) {
                    const int free_role = roles_[1] == kNoController   ? 1
                                          : roles_[0] == kNoController ? 0
                                                                       : -1;
                    if (free_role >= 0) {
                        roles_[free_role] = c;
                        mask_[c] |= kButtonOptions;
                        events.push_back({Event::Kind::Joined, c, static_cast<Role>(free_role)});
                    }
                }
                continue;
            }

            constexpr u32 combo = kButtonL3 | kButtonR3;
            if ((held & combo) == combo) {
                mask_[c] |= combo;
                if (!combo_started_[c]) {
                    combo_started_[c] = true;
                    combo_since_[c] = now_micros;
                }
                if (!combo_fired_[c] && now_micros - combo_since_[c] >= kSwapHoldMicros) {
                    combo_fired_[c] = true;
                    const Role initiator = static_cast<Role>(RoleOf(c));
                    Swap(c);
                    last_initiator_ = initiator;
                    ++sequence_;
                    events.push_back({Event::Kind::Swapped, c, initiator});
                }
            } else {
                combo_started_[c] = false;
                combo_fired_[c] = false;
            }

            if (pressed & kButtonTouchPad) {
                trico_camera_ = !trico_camera_;
                mask_[c] |= kButtonTouchPad;
                events.push_back({Event::Kind::CameraToggled, c, static_cast<Role>(RoleOf(c))});
            }
        }
        return events;
    }

    /// The physical controller playing a role, or kNoController.
    int ControllerFor(Role role) const {
        return roles_[static_cast<int>(role)];
    }

    bool AnyHuman() const {
        return roles_[0] != kNoController || roles_[1] != kNoController;
    }

    /// Buttons of a controller the game must not see (presses that drive co-op actions).
    u32 MaskFor(int controller) const {
        return controller >= 0 && controller < kPhysicalPads ? mask_[controller] : 0;
    }

    bool TricoCamera() const {
        return trico_camera_;
    }

    u16 Sequence() const {
        return sequence_;
    }

    u16 TurnCount() const {
        return turn_count_;
    }

    /// The Trico player's buttons as the scripts get them (after PressLatch, so a quick click
    /// is seen too): counts lone L3 taps (let go within kTurnTapMicros, R3 never down; L3 + R3
    /// stays the swap, and its buttons are masked out before they get here).
    void NoteTricoButtons(u64 now_micros, u32 buttons) {
        const u32 pressed = buttons & ~trico_previous_;
        trico_previous_ = buttons;
        if ((pressed & kButtonL3) && !(buttons & kButtonR3)) {
            tap_started_ = true;
            tap_since_ = now_micros;
        }
        if (tap_started_ &&
            ((buttons & kButtonR3) || now_micros - tap_since_ > kTurnTapMicros)) {
            tap_started_ = false;
        } else if (tap_started_ && !(buttons & kButtonL3)) {
            tap_started_ = false;
            ++turn_count_;
        }
    }

    u16 Flags() const {
        u16 flags = kFlagActive | static_cast<u16>(kProtocolVersion << kVersionShift);
        if (roles_[0] != kNoController) {
            flags |= kFlagBoyHuman;
        }
        if (roles_[1] != kNoController) {
            flags |= kFlagTricoHuman;
        }
        if (roles_[0] != kNoController && roles_[1] != kNoController) {
            flags |= kFlagTwoPlayers;
        }
        if (trico_camera_) {
            flags |= kFlagTricoCamera;
        }
        if (last_initiator_ == Role::Trico) {
            flags |= kFlagTricoStartedSwap;
        }
        flags |= static_cast<u16>((turn_count_ & 0x3) << kTurnShift);
        return flags;
    }

private:
    int RoleOf(int controller) const {
        for (int r = 0; r < 2; ++r) {
            if (roles_[r] == controller) {
                return r;
            }
        }
        return -1;
    }

    void Swap(int initiator) {
        tap_started_ = false;
        trico_previous_ = 0;
        const int role = RoleOf(initiator);
        const int other = roles_[1 - role];
        roles_[1 - role] = initiator;
        roles_[role] = other; // kNoController when playing alone
    }

    void ResetController(int c) {
        mask_[c] = 0;
        combo_started_[c] = false;
        combo_fired_[c] = false;
    }

    std::array<int, 2> roles_{};
    std::array<u32, kPhysicalPads> previous_{};
    std::array<u32, kPhysicalPads> mask_{};
    std::array<bool, kPhysicalPads> combo_started_{};
    std::array<bool, kPhysicalPads> combo_fired_{};
    std::array<u64, kPhysicalPads> combo_since_{};
    bool tap_started_ = false;
    u64 tap_since_ = 0;
    u32 trico_previous_ = 0;
    u16 turn_count_ = 0;
    bool trico_camera_ = false;
    Role last_initiator_ = Role::Boy;
    u16 sequence_ = 0;
};

/// SHADPS4_TLG_COOP=1 turns the co-op mode on.
inline bool EnabledByEnvironment() {
    const char* value = std::getenv("SHADPS4_TLG_COOP");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

/// The co-op scripts announce themselves by setting the second pad's light bar to this colour
/// about once a second. The emulator changes nothing until it has seen it, so a game without the
/// scripts keeps its normal controls even with SHADPS4_TLG_COOP=1.
inline constexpr std::array<u8, 3> kScriptsHelloColour{0xC0, 0x0F, 0xEE};

inline constexpr bool IsScriptsHello(u8 r, u8 g, u8 b) {
    return r == kScriptsHelloColour[0] && g == kScriptsHelloColour[1] &&
           b == kScriptsHelloColour[2];
}

/// Light bar colours of the two roles.
inline constexpr std::array<u8, 3> kBoyColour{0x00, 0x60, 0xFF};
inline constexpr std::array<u8, 3> kTricoColour{0xFF, 0x80, 0x00};

/// The game's first pad (user 1000) is the boy, the second (user 1001) is Trico.
inline constexpr Role RoleOfGamePad(int game_pad) {
    return game_pad == 0 ? Role::Boy : Role::Trico;
}

/// What the game itself sees on its second pad: nothing (sticks centred, no buttons). In the world
/// the game also reads the second pad for the boy, so the Trico player's input must not appear
/// there; it reaches the scripts through the second pad's touch points instead:
/// - touch 0 (id kTouchStickId): x = left stick x | left stick y << 8, y = right stick the same way
///   (raw 0-255 per axis, 128 = centre, 0 = left/up);
/// - touch 1 (id kTouchMagicId): x = Flags(), y = swap sequence (low 4 bits) | ScriptButtons << 4.
inline constexpr u8 kTouchStickId = 0xC1;
inline constexpr u8 kStickCentre = 0x80;

/// The Trico player's buttons, one bit each, in this order (OrbisPadButtonDataOffset values):
/// Cross, Circle, Square, Triangle, L1, R1, L2, R2, Up, Down, Left, Right.
inline constexpr std::array<u32, 12> kScriptButtons{0x4000, 0x2000, 0x8000, 0x1000,
                                                    0x0400, 0x0800, 0x0100, 0x0200,
                                                    0x0010, 0x0040, 0x0080, 0x0020};

inline constexpr u16 ScriptButtons(u32 buttons) {
    u16 bits = 0;
    for (std::size_t i = 0; i < kScriptButtons.size(); ++i) {
        if (buttons & kScriptButtons[i]) {
            bits |= static_cast<u16>(1u << i);
        }
    }
    return bits;
}

inline constexpr u16 PackStick(u8 x, u8 y) {
    return static_cast<u16>(x | (y << 8));
}

inline constexpr u16 StatePointY(u16 sequence, u32 buttons) {
    return static_cast<u16>((sequence & 0xF) | (ScriptButtons(buttons) << 4));
}

/// Keeps short presses of the Trico player's buttons visible to the scripts. The game reads its
/// pads about 17 times a second and the scripts only see what a read carries, so a tap that
/// starts and ends between two reads would be lost (the newest state shows nothing). Every press
/// seen in any state since the last read is reported, and stays reported until it has been
/// reported once and kMinPressMicros have passed since it was first seen.
inline constexpr u64 kMinPressMicros = 150'000;

class PressLatch {
public:
    /// `seen`: the buttons held in any state since the last read; `held`: in the newest state.
    /// Returns the buttons this read reports.
    u32 Read(u64 now_micros, u32 seen, u32 held) {
        const u32 fresh = (seen | held) & ~latched_;
        for (int bit = 0; bit < 32; ++bit) {
            const u32 m = 1u << bit;
            if (fresh & m) {
                since_[bit] = now_micros;
            } else if ((latched_ & m) && !(held & m) && (reported_ & m) &&
                       now_micros - since_[bit] >= kMinPressMicros) {
                latched_ &= ~m;
            }
        }
        latched_ |= fresh;
        reported_ = (reported_ & ~fresh) | latched_;
        return latched_ | held;
    }

    void Reset() {
        latched_ = 0;
        reported_ = 0;
    }

private:
    u32 latched_ = 0;
    u32 reported_ = 0;
    std::array<u64, 32> since_{};
};

/// Second-pad patch for The Last Guardian 1.03 (all regions share the executable): the game builds
/// a second pad object but stores user id -1 in it (mov dword [rbx+0x1c], 0xffffffff), so it
/// never opens it. Storing 1001 makes the game open and read the emulator's second user.
inline constexpr std::uint64_t kSecondPadPatchOffset = 0xcf8089; // from the image base
inline constexpr std::array<u8, 7> kSecondPadOriginal{0xc7, 0x43, 0x1c, 0xff, 0xff, 0xff, 0xff};
inline constexpr std::array<u8, 7> kSecondPadPatched{0xc7, 0x43, 0x1c, 0xe9, 0x03, 0x00, 0x00};

enum class PatchResult : u8 { Applied, AlreadyApplied, Mismatch, OutOfRange };

inline PatchResult ApplySecondPadPatch(u8* image, std::uint64_t image_size) {
    if (image == nullptr || image_size < kSecondPadPatchOffset + kSecondPadOriginal.size()) {
        return PatchResult::OutOfRange;
    }
    u8* code = image + kSecondPadPatchOffset;
    bool original = true;
    bool patched = true;
    for (std::size_t i = 0; i < kSecondPadOriginal.size(); ++i) {
        original = original && code[i] == kSecondPadOriginal[i];
        patched = patched && code[i] == kSecondPadPatched[i];
    }
    if (patched) {
        return PatchResult::AlreadyApplied;
    }
    if (!original) {
        return PatchResult::Mismatch;
    }
    for (std::size_t i = 0; i < kSecondPadPatched.size(); ++i) {
        code[i] = kSecondPadPatched[i];
    }
    return PatchResult::Applied;
}

/// Titles of The Last Guardian the co-op mode may patch (the bytes are still verified).
inline bool IsGuardianSerial(std::string_view serial) {
    return serial == "CUSA03745" || serial == "CUSA03627" || serial == "CUSA04936" ||
           serial == "CUSA05152";
}

} // namespace Input::TlgCoop
