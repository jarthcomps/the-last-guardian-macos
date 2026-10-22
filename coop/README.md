# Two-player co-op mod (experimental)

One player is the boy, the other is Trico. The mod is Lua scripts added to the game's update
archive (`tlg_coop.install`) plus an emulator built from the `tlg-coop` branch, started with
`SHADPS4_TLG_COOP=1`. Solo play works too: L3 + R3 swaps your controller between the boy and
Trico.

## Controls

The boy's player has the game's normal controls. Circle raises the shield, and Trico's lightning
follows the boy's aim as usual.

The Trico player:

| Input | Trico |
|---|---|
| Left stick | Move (camera-relative) |
| L2 (hold) | Aim the lightning: the shield's target marker appears ahead of Trico |
| Right stick (while L2 is held) | Move the target |
| R2 (while L2 is held) | Fire now (Trico also fires on its own while aimed) |
| Cross | Jump (to a ledge across a gap in the stick's direction, else a pounce) |
| Square | Attack (side swipe; the stick picks the side) |
| Triangle | Stomp |
| Circle | Action (on what Trico is looking at; a barrel box gets broken, food is just eaten) |
| Circle + stick left / right | Pull with the left / right paw |
| R1 | Roar |
| L1 (hold) | Threat posture |
| D-pad up (hold) | Rear up on the hind legs |
| D-pad down | Sit; press again to lie down, again to stand up |
| D-pad left | Stretch |
| D-pad right | Hind-leg kick |

For both players:

| Input | |
|---|---|
| L3 + R3 | Swap roles (a lightning arc runs along Trico) |
| Select (touchpad) | Switch the shared camera between the boy framing and the wider Trico framing |

Trico's own AI keeps running: eating, following, climbing, swimming and its own jumps happen as
usual. Buttons only add requests, and a move is only sent while Trico stands, waits or sits.
The lightning works only while the boy has the shield.

## Files

- `lua/`: the scripts:
  - `coop_main`: entry point;
  - `coop_input`: emulator data;
  - `coop_trico`: steering;
  - `coop_actions`: buttons;
  - `coop_pulse`: lightning;
  - `coop_camera`: the shared camera;
  - `coop_swap_fx`: the swap arc;
  - `coop_debug`: the log channel;
  - `coop_config`: settings.
- `tlg_coop/`: the installer (`python3 -m tlg_coop.install`), the PSARC reader and writer, and the test-run probe.
- `tests/`: `python3 -m unittest discover -s tests -t .`. The Lua suites in `tests/lua` run under `lua5.1` or `luajit`.
