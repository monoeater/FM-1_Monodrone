# MONODRONE

A drone instrument for the FM-1: four layers of drone, each a latched chord of up to four voices on one of twelve
engines, into one effects chain (dirt, filter, tape delay, reverb), with three punch effects and a GLOBAL page. It
has no sequencer: you set things running, change them live, and jump between them.

**Status (L25):** your first test ran it at 17-20 % load, with the second core working. L5 adds polyphony, five new
engines, the new keyboard layout, the GLOBAL page, a deeper VOID, a level-matched BURN and darker scenes. It passes
the host simulator's tests and an 8-minute random fuzz under AddressSanitizer. Its worst case on the computer is
four layers of four CHOIR voices, about 2.5x the instructions of your test. That should be around 45-55 % on the
FM-1, and the CPU guard is still there.

## Quick start

1. Listen to `demos/engines/` (one MP3 per engine), `demos/punches/` (each punch over a scene: 3 s before, 4 s held, 3 s after) and `demos/` (one per factory scene).
2. Flash `builds/monodrone.fwsc`.
3. Press **PLAY**: the scene swells in. Play chords on the keys (they latch), turn **ALGO** for the engine and
   **PRESET** for the scene, and tap **FX** to make the keys switch the punches (tap again for notes).

## Flash it

`builds/monodrone.fwsc` is always the newest; `-L1`..`-L5` are kept so you can step back.

    py -3.12 \\wsl.localhost\Ubuntu\home\michael\Felucca\tools\fm1_install.py --force C:\Users\Michael\Monodrone\builds\monodrone.fwsc

To get back to stock: hold **OCT-** while you switch it on, then install M-VAVE's V15 or any other firmware.

## The panel

| Control | Does |
| --- | --- |
| **Keys** (all 27) | Notes. Keys held together build a chord on the selected layer (up to 4 notes; a fifth replaces the last). Let go and it stays latched. The next key played afresh starts a new chord, and the voices glide from the old notes to the new. |
| **Key pressure** | Swells the chord you're holding: louder, with K1 pushed up. |
| **FX** | A mode (lit): the keys switch the punches instead of playing notes. Hold a key for the punch; hold it 2 s, or tap it twice, to latch; press it again to release. Latched punches stay on when you tap FX again to go back to notes. See the table below. |
| **ALGO in FX mode** | The effects pages: DIRT, FILTER, DELAY, REVERB (FX mode opens on the last one) |
| **SELECT** | Selected layer (1-4). While you hold a chord, the knobs and ALGO act on the chord's layer. |
| **ALGO** | The layer's engine (the only place to change it) |
| **PRESET** | Scene 1-16, gliding there over MORPH while playing |
| **K1-K4** | The page's four controls |
| **HOME** | The selected layer's own pages, one per press: LAYER (the engine's three controls + LEVEL), SHAPE, OUT (LEVEL, PAN). Holding HOME and turning SELECT steps through them too. Turning SELECT to a new layer goes back to LAYER. |
| **EDIT** | SHAPE: TUNE, GLIDE, SWELL, DRIFT |
| **ENV** | MACRO, MACRO 2, GLOBAL in turn (from any other page, MACRO; it moves on when you let go). **Hold ENV and turn a macro knob** to snap it: clockwise to full, anticlockwise off. **ENV + GLO**: every macro off. MACRO 2: TANGLE, the layers that are on modulating each other in a ring (1 → 2 → 3 → 4 → 1, skipping the ones off): the lower half lets each layer's loudness move the next one's K1, the top half adds ring modulation by the next one's sound. Then three from MONOEATER.HARSH: RUNAWAY (every feedback pushed toward screaming: HOWL feedback and excitation, SCRAPE's metal, ROT's feedback, filter resonance, delay feedback, and the output fed back into the dirt through a short wandering line; at full it screams by itself), STARVE (a dying supply: layers sputter out, pitches sag and waver, CRUSH up, and past a quarter the whole output drops out now and then), DELUGE (a bubbling, hammered sub at the lowest note's pitch folded into 40-75 Hz under a pinned, scooped, wide wall; the sweep and UNREST calmed). Every macro's last 40 % goes further, and the level is held within about 1 dB. MACRO: four knobs that each move many settings at once, on top of yours (0 = off): ROT (drive, crush, downsampling, dirt mix), VAST (bigger, wetter reverb; longer, wetter echoes), NAUSEA (detune, unrest, drift, faster and deeper weather), WEIGHT (sub up, filter, reverb and echoes darker). Not saved: they're for playing. GLOBAL: DETUNE (every voice and layer pulled apart, so they rub and beat), SUB (a sine an octave or two under the lowest note sounding), HISS (tape hiss, 50 Hz mains hum, the odd crackle), UNREST (each voice's pitch and level unsteady, plus tape flutter and dropouts on the delay) |
| **GLO** | MIX (four levels), press again for PAN. Hold GLO and tap white keys 1-4 to bring layers in or out. |
| **LFO** | WEATHER: SPEED, and how far the weather moves each layer's TIMBRE (its K1), the FILTER and the ROOM (delay and reverb). Press again for SCENE (MORPH, PLACE). |
| **PLAY** | The scene's layers in or out |
| **SEQ** | The selected layer in or out (the mute) |
| **ARP** | FREEZE |
| **REC** | SMEAR's memory: recording (lit) or held |
| **SAVE** | Saves to the current slot. Hold SAVE and tap a white key to save into that slot. **Press SAVE twice** to rename the scene: K1 moves the cursor along its 8 letters, K2 changes the letter (A-Z, 0-9, space, - . / # and µ); SAVE again keeps the name and saves, any other button leaves it as it was. |
| **SEL** + white key | Recalls that scene. **SEL + PLAY** = MUTATE. |
| **SEL held + any knob** | For now: turn anything on any page, let go of SEL and it all springs back (a scene recalled or MUTATE while SEL is held stays). |
| **OCT- / OCT+** | The keyboard's octave |
| MIDI in | Notes build and latch chords like the keys. CC1 K1, 7 level, 74 cutoff, 71 resonance, 91 reverb, 93 delay; aftertouch is pressure. |

### Punches (FX mode)

While a punch's key is held, K1-K3 are its own controls and K4 is its FADE: the same scale on every punch, 0-10 s, in and out alike (finer at the short end), shown in MS/S and the screen shows them; let go and you're back on your page. Turning them stops the 2-second latch, so you can hold and shape without it sticking. They're saved with SAVE (one set for all scenes).

| Key | Punch | What it does | Held: K1, K2, K3 (K4: FADE) |
| --- | --- | --- | --- |
| F#3 | BURN | crushed fuzz, held to the incoming level | FUZZ, BITS |
| G#3 | VOID | a held wash like MONOEATER's: the first half second pours into the reverb, then it holds forever and the dry falls away; release and it fades out | SOAK, DRY, DARK |
| A#3 | SHATTER | glitch slices of the last moment, sometimes reversed | SIZE, REPEAT, REVERSE |
| C#4 | WARBLE | a slow, soft pulse: a gentle wow and a dip in level and brightness every ~6 s, easing in | RATE, DEPTH, WOW |
| D#4 | INFINITE | a black hole: the longer you hold it, the more everything is pulled down into a darkening, slowing, endless smear and the dry disappears | FALL, DARK, DRAG |
| F#4 | SHIMMER DOWN | the reverb tail is pitched an octave down and fed back in, cascading lower | MIX, FEED |
| G#4 | THROW | the delay takes everything in and runs near unity; let go and it rings off | FDBK, ECHO |
| A#4 | RING | ring mod against a low, unsteady carrier (30-250 Hz), easing in over ~0.7 s | FREQ, WOBBLE, MIX |
| C#5 | DECIMATE | down to ~2.7 kHz and 5 bits | RATE, BITS |
| D#5 | GATE | a failing supply: chopped on and off at uneven lengths | SPEED, DEPTH |
| F#5 | SCREAM | locked feedback on a harmonic of the lowest note, driven and distorted: a steady squeal that barely moves | PITCH, FDBK, DRIVE |
| F3 (lowest white) | ROTATE | the stereo field turns, unsteadily | SPEED, WOBBLE |

Some punches also show in the picture: WARBLE wavers it, DECIMATE blocks it up, SCREAM tears lines, GATE blacks it
out, alongside the existing BURN, VOID and SHATTER.

### Engines (K1 / K2 / K3)

| Engine | What it is | K1 / K2 / K3 |
| --- | --- | --- |
| HIVE | Lyra-ish pair of cross-FM sines into a sharpening clip, with a sub | BEAT / CROSS / SHARP |
| SWARM | five detuned saws into a resonant low-pass (Sunn/Boris wall) | SPREAD / BRIGHT / GRIT |
| ROT | two-op FM with modulator feedback; 16 mostly inharmonic ratios | RATIO / INDEX / FEEDBK |
| WIND | noise through two resonant band-passes + brown rumble | FOCUS / SWEEP / RUMBLE |
| HOWL | tuned feedback loop, saturated; past ~70 % it screams on its own | FEEDBK / DAMP / EXCITE |
| GEIGER | random clicks, raw or ringing at pitch; fire crackle bursts | RATE / RING / FIRE |
| SMEAR | grains of the last 1.5 s of the other layers, at the chord's pitches | SIZE / SCATTER / DENSITY |
| **HUM** | transformer: mains tone rectified into buzz, a loose lamination rattling, sag and 100 Hz ripple | BUZZ / RATTLE / SAG |
| **SCRAPE** | bowed and scraped metal: four plate resonances driven by friction noise and stick-slip | PRESS / METAL / GRIND |
| **CHOIR** | a dead choir: three voices apart, through dark vowel formants, with breath | VOWEL / BREATH / CROWD |
| **STATIC** | shortwave between stations: static, crackle, a drifting heterodyne whistle, a keyed signal fading | TUNE / SIGNAL / FADE |
| **CHAOS** | the logistic map clocked at the pitch, bending a sine; the clock fractures to other rates | CHAOS / BREAK / GRIT |

PULSE is gone.

### Scenes

| # | Name | Layers | Place |
| --- | --- | --- | --- |
| 1 | ZONE | HOWL chord, HUM at 49 Hz, GEIGER, SCRAPE chord | reactor |
| 2 | DRONEVIL | SWARM fifths x2 (heavy dirt, SUB), HOWL, CHAOS | peaks |
| 3 | LYRA | HIVE chords, low HOWL | dunes |
| 4 | DUST | WIND, GEIGER fire, STATIC, ROT feedback; crushed, lots of HISS | bunker (tunnel) |
| 5 | CORTINI | SWARM triad, CHOIR, SMEAR, HIVE; long reverb | peaks |
| 6 | RADIO | two STATICs, raw GEIGER, CHAOS; band-passed | factory |
| 7 | REACTOR | HUM chord, screaming HOWL, SCRAPE, GEIGER storm | reactor |
| 8 | ASH | SMEAR, dark CHOIR (oo), WIND, SMEAR | dunes |
| 9 | INFECTUM | low CHOIR triad, HUM at 25 Hz, SMEAR, high HOWL; huge reverb, lots of SUB | reactor |
| 10 | MEATGRND | SWARM fifths, CHAOS, SCRAPE, ROT; driven hard | factory |
| 11 | REMOTE | STATIC, WIND, sparse HIVE, slow GEIGER; long echoes, hiss | channel |
| 12 | SOYLENT | two beating HOWL pairs, CHOIR, CHAOS; fast weather, heavy UNREST | bunker |
| 13 | µSIEVERT | dense GEIGER, HUM at 49 Hz, STATIC, HIVE | control |
| 14 | ABYSS | WIND, SMEAR, CHOIR, low HUM; wide and washed | dunes |
| 15 | FROST | SWARM hard left, HOWL hard right, ROT and SCRAPE apart | peaks |
| 16 | OMEGA | SCRAPE, HUM, GEIGER, ROT; an industrial room | factory |

L13 upgrades saved scenes: a slot 9-16 still as the factory left it becomes its new scene; one you saved over keeps
your sound and takes the new name. (The name font draws µ for ^.) **Saved L1-L4 scenes don't carry over** (the format changed for chords); MONODRONE starts
from the factory set.

## If it crashes

fm1-base keeps a crash record that survives a restart. `py base/tools/fm1_remote.py listen wait 2000 screen log`
reads it over USB; the installer page's "Report a problem" shows it too.

## The choices I made

- **Base: fm1-base + felucca-bsp** (the base Loom and Frasil use), not fm1_open/MONOEATER. fm1_open's binary
  engine fills the flash and leaves about 3 KB free. fm1-base starts empty: the whole firmware is about 120 KB, and
  big buffers have a 336 KB RAM pool (MONODRONE uses 243 KB of it). It also has a host simulator, so I could test
  the panel and render audio and screenshots overnight. Your MONOEATER folder is untouched.
- **Float DSP only.** felucca-bsp's `docs/CPU.md` documents an AC79 fault: inlined 64-bit integer multiplies next
  to 16-bit loads corrupt registers now and then, and the firmware crashes. MONOEATER's `audio_output.c` does
  exactly that (int64 multiplies plus int16 buffers). If MONOEATER ever crashes at random, that's a likely suspect.
- **Two cores.** Core 1 renders layers 3 and 4. Some FM-1s misread RAM on the second core, so it is built with the
  fallback: after a failure it drops to one core until power-off. The header then shows `1C` next to the load %.
- **Licence: MPL-2.0** for the app, so Bluetooth could be added later. fm1-base is MPL-2.0 too.
- **No code copied** from the other firmwares. I read their READMEs for ideas: Frasil's pressure swell, GR!MM's
  freeze, and Loom's build layout.

## Build it yourself

WSL/Linux, with the JieLi toolchain (`base/tools/get_toolchain.sh`) and the three AC79 SDK files you copied:

    git submodule update --init --recursive
    AC79_SDK=/mnt/c/Users/Michael/Monodrone/_import/sdk-tree ./build.sh     # firmware: app/build/monodrone.fwsc
    ./build.sh --host && app/build/host/monodrone_host app/tests/basic.txt app/build/host   # the simulator

`AC79_SDK` must point at a folder laid out like the SDK: `cpu/wl82/tools/uboot.boot`, `cpu/wl82/tools/cfg_tool.bin`
and `cpu/wl82/tools/cfg/eq_cfg_hw.bin`. `_import/sdk-tree/` has that layout.

`tools/dsp_render.c` renders each engine to WAV on the computer (see its header).

## Flash map

The scenes are saved at 0xD4000-0xD5FFF, in the data area the Felucca-family firmwares share. Saving there can
overwrite another firmware's data at those addresses (Loom's sample bank lives near there). MONODRONE only reads
its own records (magic `MDNO`), so another firmware's data never confuses it.

## Log

- L1: the base works on fm1-base. Four layers, eight engines, effects, scenes, save/recall, MIDI in, two cores.
- L2: eight named factory scenes, MORPH and MUTATE, engine levels balanced, the second core rendering layers 3-4.
- L3: GLO + keys 1-4 bring layers in and out; times shown in ms/s; demos rendered. A held key's layer takes the knobs; four-diffuser reverb; BURN and VOID level-matched; the animated places.
- L4: CPU guard.
- L25: rename a scene: SAVE pressed twice, K1 the cursor, K2 the letter, SAVE keeps it.
- L24: BUNKER redrawn as an old train tunnel deep underground: a ribbed concrete vault running to a bright far end, a water stain, lamps along the crown (they flicker, one now and then dies), rails, rusted racks of boxes down both sides, a wooden X-braced door in the left wall, and a pile of junk in the near left (stacked boxes, a plank crate, sacks, a bucket, a pipe, rubble) dark against the light. Frame folders renamed with the scenes' new names.
- L23: five scenes renamed: WISHGRNT is INFECTUM, PSYKOSYS is SOYLENT, STRAND is ABYSS, DIVIDE is FROST, ANNEX is OMEGA. A saved scene still carrying an old name takes the new one (its sound is kept).
- L22: rebuilt on the latest fm1-base (c8e0599, 185 commits on: newer FM-1 flash chips (UC25HQ80) and loaders, the fast install, installers' identity fixes, the update loader writing resources). Nothing in MONODRONE changed.
- L21: a seventh place, CHANNEL: a city's concrete drainage channel under an expressway (lit beam and railings, dark blocks with a few windows, a sign on a lattice), the wall stained and sprayed, two rusted pipes curving out of it and running down to the ground. Scene 11 uses it. PLACE (SCENE page K2) now moves one place a click. Places stored place*16+8 (store v10, earlier saves read); once you SAVE in L21, L20 and earlier start from the factory scenes.
- L20: DUNES reworked: the mounds scattered and lopsided (small and crowded far off, big near, overlapping); the standing figure a silhouette after the film (side-on facing right, head turned to us, collar up, long coat), the two at the door walking, all with a thin lit outline so they hold against the dark.
- L19: REACTOR, BUNKER and FACTORY redrawn in the same style, so all six places match: REACTOR's towers lit and ringed with stains, the block, the sarcophagus slope and the striped stack, wind streaks; BUNKER under a big earth mound, its face lit from the left, graffiti and the zone's mark, coiled wire, a bent mast; FACTORY's high windows full of light, lit trusses and columns, the floor going away.
- L18: WASTE replaced by DUNES, after the sand room in Stalker: a dark hall of cracked stone pillars, a misty doorway glowing at the back with two small figures in it, rows of sand mounds lit along their crests, a lone dark figure among them, dust turning in the light. LYRA, ASH and STRAND use it.
- L17: PEAKS and CONTROL redrawn in the reference's style (big white masses on black, one light from the left, lit faces cracked and shadow faces hatched, 50 % checker for the middle tones): PEAKS one range tapering off to the right with a lower ridge in front, cumulus heaps drifting behind, wind streaks, pines in mist; CONTROL with pale socket walls, the core's big tiled disc, the curving window wall and a hatched console.
- L16: a sixth place, CONTROL: the reactor's dead control room (walls of empty dial sockets, the core's round mimic board losing tiles and still blinking, curved consoles, rods hanging; the lights stutter now and then). Scene 13 uses it; PLACE (SCENE page) reaches it. Saved scenes keep their places (store v9); once you SAVE in L16, L15 and earlier start from the factory scenes.
- L15: names tidied so no two controls share one: MACRO's SPACE is VAST and UNREST is NAUSEA (GLOBAL keeps UNREST); WEATHER's TONE / SPACE are TIMBRE / ROOM; HIVE's DETUNE is BEAT (GLOBAL keeps DETUNE).
- L14: hold ENV and turn a macro knob: full or off at once; ENV + GLO: every macro off (both from MONOEATER.HARSH). ENV moves on when let go.
- L13: scenes 9-16 their own (WISHGRNT, MEATGRND, REMOTE, PSYKOSYS, µSIEVERT, STRAND, DIVIDE, ANNEX; saves upgraded). The macros' top 40 % more extreme (ROT ruined, VAST endless, NAUSEA seasick, WEIGHT buried), level held. MACRO 2 K2-K4: RUNAWAY, STARVE, DELUGE from MONOEATER.HARSH.
- L12: FADE in seconds, 0-10 s, the same for every punch, in and out alike. Saves from L9-L11 get the punches' starting fades.
- L11: K4 is FADE on every punch's held page (both ways, saved). L11 reads L9/L10 saves; once you SAVE in L11, L10 and earlier start from the factory scenes.
- L10: HOME presses step through the layer's pages; SELECT to a new layer starts on LAYER. MACRO 2 page with TANGLE (cross-modulation between the layers that are on).
- L9 note: L9 reads scenes saved by L8, but once you SAVE in L9, going back to L8 or earlier starts from the factory scenes.
- L9: hold HOME + turn SELECT for a layer's pages (new OUT page: LEVEL, PAN); MACRO page on ENV (ROT, SPACE, UNREST, WEIGHT); hold a punch and twist K1-K3 (its own controls, saved); hold SEL to turn knobs for now and spring back on release.
- L8: FX is a mode (keys switch punches; held 2 s or tapped twice latches; ALGO turns the effects pages). Keys no longer bring a muted layer in: SEQ and PLAY do. VOID, WARBLE, INFINITE and SCREAM reworked again.
- L7: VOID, WARBLE, INFINITE, RING and SCREAM reworked after your review; fixed a crash in INFINITE with the reverb's MOD high.
- L6: nine more punches: WARBLE, INFINITE, SHIMMER DOWN, THROW, RING, DECIMATE, GATE, SCREAM, ROTATE.
- L5: polyphony (latched chords), HUM / SCRAPE / CHOIR / STATIC / CHAOS (PULSE gone), every key a note (punches on FX + black keys, engines on ALGO only), GLOBAL page (DETUNE, SUB, HISS, UNREST; tape dropouts), VOID deeper and fading in, BURN level-matched, darker factory scenes.

## Housekeeping

- `_import/` holds what I needed overnight: the MONOEATER snapshot I read, a partial copy of the AC79 SDK (the copy
  you stopped, about 300 MB, safe to delete), `sdk3/` (the three files) and `sdk-tree/` (those three in the SDK's
  layout, for building). It also has the `md_sync*.tgz` files I used to copy work across, which you can delete. I
  couldn't delete anything in your folders myself.
- On Windows, git may show every file as modified. That's file-mode noise:
  `git config core.fileMode false` clears it.
- The repo's history is in `.git`, one commit per step.

## L26: CITY, an eighth place
- A foggy city: a huge black tower in a glow of fog with the sun behind, cables sagging into it, a pylon box hanging,
  mansard roofs (far row hazed, near row black with lit ridges, dormers, lit windows), a balcony rail and a brick wall
  with a dish up close. Tower lights blink, fog drifts over the roofs, something flies across.
- Not on any factory scene yet: reach it with LFO › SCENE › K2 PLACE (the last place).

## L27: CITY detail
- Tower: panel seams, lit ports, flanges, a scaffold, a mast. Roofs: slate courses, cornices, windows, chimney pots, TV aerials. A wooden pole up close with crossarms, insulators and wires.

## L28: SEL + OCT
- Hold SEL, tap OCT- / OCT+: the selected layer's chord moves a whole octave (gliding), kept when SEL is let go; refuses past MIDI 12..108 ("AT THE END"). The keyboard octave is untouched. Test: seloct.txt.

## L29: GLIDE 3 ms by default
- dr_defaults: glide 0 (= 3 ms) for every layer of every factory scene. SC_VERSION 11: a store from L28 or before has its untouched GLIDE 30s set to 0; anything else set is kept. Test: glide.txt.

## L30: RUNAWAY pitch
- Tested all 16 scenes at full RUNAWAY: its loop always whistled at ~1.5-2.8 kHz whatever the scene (a fixed 600 Hz-3.4 kHz band wandering round its middle). Now its band sits on a harmonic of the lowest note, picked at random over 150 Hz-2.4 kHz each time the knob comes up from off and every 3-9 s after (gliding), and the line is a whole number of that pitch's periods.

## L31: RUNAWAY high, RUNOFF low, WEIGHT gone
- MACRO: ROT VAST NAUSEA STARVE. MACRO 2: TANGLE RUNAWAY RUNOFF DELUGE. WEIGHT removed.
- RUNAWAY and RUNOFF: the same feedback (HOWL/SCRAPE/ROT feedback, resonance, delay FDBK pushed) with a loop tuned a fifth above the lowest note (3rd harmonic), octaved into 1.4-2.8 kHz (RUNAWAY) or 110-220 Hz (RUNOFF). No random movement: they follow the scene, gliding when the lowest note changes; each loop line a whole number of its periods. Checked on all 16 scenes: steady, different per scene.

## L32: RUNOFF lower
- RUNOFF's loop now 70-140 Hz (between L31's 110-220 and the SUB); its level trim 4 dB at full instead of 7.5.

## Hub prep
- Identity prefix FM-1_77 (dev FM-1_7700; release 0.0.1 = FM-1_77000001, 1.0 = FM-1_77010000). LICENSE (MPL-2.0), fmhub.json, docs/img/screen.png, fm1.toml author/description/tags/screenshot. Still needed for a release build: repo, install.

## L33: HOME leaves FX mode
- HOME tapped in FX mode: FX mode off (KEYS NOTES), the LAYER page; latched punches carry on. Test: homefx.txt.
