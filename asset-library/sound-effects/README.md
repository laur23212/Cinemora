# Dafin Sound Effects

335 original sound effects made for Dafin by ChatGPT ("Dafin Sound Design / ChatGPT"), from the
handoff `Dafin_SFX_v4_Expanded` (package 4.0-expanded, 2026-11-11). Built into the app and listed
in Audio › Online › Dafin Sound Effects.

**Rights.** All of them are procedural synthesis (oscillators and seeded noise). They contain no
recordings, commercial samples or third-party sounds. They are delivered as royalty-free
originals for Dafin, and using them needs no attribution. This is **not** a CC0 dedication, so
don't relabel them as CC0. The 55 effects withdrawn from earlier deliveries are not here. Neither
are the 44 CC0 recording candidates (camera shutters, doors, applause, rain…), which were listed
but never acquired.

**Processing here.** `tools/sfx/build_sfx_index.py` re-encoded the 48 kHz WAV masters to Ogg
Opus at 192 kbps. Effects louder than −8 LUFS integrated were turned down to −8, and every
decoded file peaks at or under −1 dBTP. Nothing was turned up. Each entry's `gainDb` in
`index.json` records the change. The masters themselves are not in the repository.

**Not done yet.** No human has listened through the whole set. The owner approved the
direction, not each of the 240 new v4 effects.
