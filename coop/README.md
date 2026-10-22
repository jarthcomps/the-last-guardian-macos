# Two-player co-op for The Last Guardian (experimental)

One player is the boy, the other is Trico. Trico keeps its own mind: it still follows the boy,
eats, climbs, swims and jumps by itself. The Trico player steers it and asks it for moves, so
it plays like a very willing Trico rather than a remote-controlled one.

## What you need

- The Last Guardian (CUSA03745) with **update 1.03**, extracted, as the launcher already uses it.
- The macOS launcher built with the co-op option (it ships the co-op emulator and the scripts).
- Two controllers for two players. One controller works too: see "Playing alone".

## Turning it on

1. In the launcher, turn on **Two-player co-op (experimental)**.
2. Press **Play**. The launcher adds the co-op scripts to its own copy of the game's update
   archive (your game folders are not changed) and starts the game in co-op mode.
3. To go back to the normal game, turn the option off and press Play: the scripts are removed.

Saves are the game's normal saves; co-op doesn't change them.

## Who is who

- Controller 1 starts as the boy, controller 2 as Trico.
- **L3 + R3** (both sticks pressed in) swaps the roles, on either controller. A bolt of lightning
  jumps between Trico's tail and the boy's head, starting from whoever swapped.

## The boy

The game's normal controls. Circle raises the shield (once the boy has it), and Trico's
lightning follows the boy's aim as usual.

## Trico

| Input | Trico |
|---|---|
| Left stick | Walk and run where you point (relative to the camera) |
| Right stick | Turn the camera around Trico (see "The camera") |
| Cross | Jump: to a ledge across a gap in the stick's direction, otherwise a pounce |
| Square | Swipe (the stick picks the side) |
| Triangle | Stomp |
| Circle | Eat a nearby barrel of food; otherwise act on what Trico is looking at (a barrel box gets broken) |
| Circle + stick left / right | Pull with the left / right paw |
| L2 (hold) | Aim the lightning: the shield's target marker appears ahead of Trico |
| Right stick, while L2 is held | Move the target |
| R2, while L2 is held | Fire now (Trico also fires by itself while aimed) |
| R1 | Roar |
| L1 (hold) | Threat posture (let go to end it) |
| D-pad up (hold) | Rear up on the hind legs |
| D-pad down | Sit; press again to lie down, again to stand up |
| D-pad left | Stretch |
| D-pad right | Hind-leg kick |

Moves happen when Trico can do them. If it is sitting or walking it first stands up or stops,
then does the move. A second press made meanwhile waits its turn behind the first (two at most;
a move that can't happen within about 8 seconds is dropped).

## The camera

Both players share one screen. **Select** (the touchpad button) switches between:

- **the boy's camera**: the game's own camera, as in normal play;
- **the Trico camera**: a wider view from behind and above Trico. When Trico or the boy leaves
  the shot it backs out and looks down more steeply, then eases back once both are in view.

Both players can turn the camera with the right stick: the boy's player as in the normal game,
the Trico player whenever L2 isn't held. Whoever moved a stick last has the camera. About three
seconds after the Trico player lets go, the camera swings back behind Trico.

Cut-scenes and scripted camera moments always take over; the Trico camera comes back after them.

## Playing alone

Use one controller and press L3 + R3 to switch between the boy and Trico. While you play Trico
the boy stands and waits.

## Known limits

- Experimental: expect rough edges, and keep a copy of saves you care about.
- Trico's judgement comes first. It won't jump where its AI sees no landing, and button presses
  are requests, not commands.
- Jumps only find ledges across gaps the game marks; some story jumps still need the boy to call
  Trico, as in normal play.
- The lightning works only once the boy has the shield.
- On narrow ledges the Trico camera can end up close to Trico when turned toward a wall.
- The mod doesn't change the frame rate: on the test Mac the same spot ran at about 16 FPS with
  and without it (the emulator's limit).

## For developers

Without the launcher: `python3 -m tlg_coop.install install <path to CUSA03745>` (`uninstall` and
`status` do what they say), then start an emulator built from the `tlg-coop` branch with
`SHADPS4_TLG_COOP=1` and the second-controller patch.

- `lua/`: the scripts:
  - `coop_main`: entry point;
  - `coop_input`: emulator data;
  - `coop_trico`: steering;
  - `coop_actions`: buttons and the move queue;
  - `coop_pulse`: lightning;
  - `coop_camera`: the shared camera, its framing and the Trico player's orbit;
  - `coop_swap_fx`: the swap arc;
  - `coop_debug`: the log channel;
  - `coop_config`: settings.
- `tlg_coop/`: the installer, the PSARC reader and writer, and the test-run probe
  (`python3 -m tlg_coop.probe`; `--no-mod` runs the same setup without the scripts).
- `tests/`: `python3 -m unittest discover -s tests -t .`. The Lua suites in `tests/lua` run under
  `lua5.1` or `luajit`.
