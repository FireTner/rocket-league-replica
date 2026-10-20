# Arena draw-bucket plan (G3a sweep, 2026-10-17)

Read-only investigator plan for the G3b builder. Goal of G3: arena
`draws_last=325` → `<60` via 4 merged static buckets, tris ±1%, VG1
byte-stable. G1 baseline (A-box): `tris_last=189126 draws_last=325`
(`src/render/BUILD_NOTES.md` G1 entry). G2 landed: GL 4.5-compat hard-fail
(`src/render/Renderer.cpp:28-34,64-94`, `src/game/main.cpp` pre-Init attrs),
`[render] msaa=0` default, `build/game` sha256
`ebc47202f789af809a183ea37e86964754a978c6cbab479a9273e79e8bdeb2dd`.
VG7 runs WITH `--meshdir` (`tools/verify/visual_gates.py:606`), so the
baseline below is **mesh mode** (`roundCorners=true`,
`src/game/main.cpp:1099`); fallback column is given for byte-parity.

Nothing here is implemented. G3b owns all code.

## 1. Bucket table (mesh mode; fallback deltas in §3)

`tris` = the helper's counted cost (`CountTris`/`NoteExternalDraw`).
`draws` = counted draws (one per helper call / external path).

### Bucket (a) — opaque colored, untextured, lit, depth-write ON

| # | Call site | Helper | tris | draws | Notes |
|---|---|---|---|---|---|
| A1 | `main.cpp:1326-1328` (mesh trim bars, 8×) | `DrawBox` (`Renderer.cpp:230`) | 96 | 8 | Brushed steel, `a=1`. Fallback twin: `main.cpp:1301-1306` (same count) |
| A2 | `main.cpp:1410-1416` cross beams+reveals (9×2) | `DrawBox` | 216 | 18 | `a=0.9` but depth-write ON → keep in opaque pass, preserve order |
| A3 | `main.cpp:1419-1422` long beams+reveals (7×2) | `DrawBox` | 168 | 14 | Same `a=0.9` note as A2 |
| A4 | `main.cpp:1499-1506` cove shadow lines (4×) | `DrawBox` | 48 | 4 | `a=0.9`, inset 3 uu below cove |
| A5 | `main.cpp:1522-1524` goal posts/crossbar (3×2 goals) | `DrawBox` | 72 | 6 | White, `a=1` |
| A6 | `main.cpp:1529-1532` post-top spheres (2×2 goals) | `DrawSphere` s6 (`Renderer.cpp:260`) | 288 | 4 | `2*6*6=72` tris each; mouth-exterior only |
| **(a) total** | | | **888** | **54** | → merges to **1 VBO draw** (no texture) |

### Bucket (b) — textured opaque (`a=1`), depth-write ON, depth-test ON — EXCEPT B2

| # | Call site | Helper | tris | draws | Texture |
|---|---|---|---|---|---|
| B1 | `main.cpp:895-896` turf floor | `DrawTexturedQuadXZ` (`Renderer.cpp:488`) | 2 | 1 | `TurfTexture()` |
| B2 | `main.cpp:899-900` markings overlay | `DrawTexturedOverlayXZ` (`Renderer.cpp:539`) | 2 | 1 | `TurfMarkingsTexture()` — **depth-TEST OFF**; must stay its own draw (§5) |
| B3 | `main.cpp:1118-1176` floor-fillet bases (2 side + 4 mouth-split + 4 chamfer) | inline `arcSweep` (`main.cpp:917-1006`), `baseTex=WallBaseTexture()` | 288 | 10 | Side runs 24 tris; mouth segs 36 (1 end-cap fan of 12); chamfers 24 |
| B4 | `main.cpp:1405-1407` ceiling panel | `DrawTexturedQuadXZ` `faceDown=true` | 2 | 1 | `CeilingTexture()` — **V5-sensitive** (§4) |
| B5 | `main.cpp:1460-1477` outer skirt straights+chamfers (4+4) | `DrawTexturedQuadWall` (`Renderer.cpp:515`) | 16 | 8 | `PlainGlassTexture()`, dark tint, `v≈0.9` |
| B6 | `main.cpp:1488-1495` round skirts (mesh only, 8×) | `planRound` (`main.cpp:1029-1064`), 8 steps | 128 | 8 | Same skirt tex, `v 0.9→0.93` |
| B7 | `main.cpp:1374-1375` round bases (mesh only, 8×) | `planRound`, 8 steps | 128 | 8 | `WallBaseTexture()` |
| **(b) total** | | | **566** | **37** | → **5 VBO draws** (one per texture: turf / markings / wallbase / ceiling / skirt) |

### Bucket (c) — transparent (`a<1`), drawn AFTER all opaques, sorted last

| # | Call site | Helper | tris | draws | Notes |
|---|---|---|---|---|---|
| C1 | `main.cpp:1237-1254` flat glass (2 side + 4 back segs + 2 tops) | `DrawTexturedQuadWall` via `glassSub` (`main.cpp:1210-1223`) | 24 | 8 | `kGlassA=0.40`, `HexNetTexture()` (or Plain when `--no-hexnet`) — **V6-sensitive** |
| C2 | `main.cpp:1289-1292` corner glass (4×) | `DrawTexturedQuadWall` via `cornerGlass` (`main.cpp:1263-1281`) | 8 | 4 | Negative `uRep`, inward π-rotation — **V5-sensitive** |
| C3 | `main.cpp:1333-1364` crown fillets (8×) | `arcSweep` ceiling-course, `crownTex` | 192 | 8 | `v` continues glass field (`crownV0→crownV1`); radial normals |
| C4 | `main.cpp:1376-1381` round glass+crown (mesh only, 8 junctions ×2) | `planRound` ×2 per junction | 256 | 16 | Faceted (flat mid-angle normals), arc-length U |
| C5 | `main.cpp:1579-1580` goal interior shells (2×) | `DrawBox`, `a=0.30` | 24 | 2 | AFTER cars/ball; **depth-TEST on, depth-WRITE OFF** (`main.cpp:1568-1570,1592`) |
| C6 | `main.cpp:1586-1590` goal weave backs (2×) | `DrawTexturedQuadWall`, `a=0.55` | 4 | 2 | `GoalInteriorTexture()`; weave yaw per-goal — **V5-sensitive** |
| **(c) total** | | | **508** | **40** | → **3 VBO draws** (hexnet / untext untextured-shell / weave); C5+C6 keep depth-mask-off |

### Bucket (d) — emissive/glow boxes (today: same lit-`DrawBox` state, bright colors)

| # | Call site | Helper | tris | draws | Notes |
|---|---|---|---|---|---|
| D1 | `main.cpp:1431-1448` cove glow strips (4 + 4 mesh diagonals) | `DrawBox`, `a=0.9` | 96 | 8 | (0.45,0.72,1.0); diagonals mesh-only |
| D2 | `main.cpp:1518-1520` goal trim frames (3×2 goals) | `DrawBox`, `a=0.95` | 72 | 6 | Team tint; fallback-identical |
| **(d) total** | | | **168** | **14** | → **merge INTO (a)** (1 draw): `DrawBox` never disables lighting, so state matches (a) exactly; keep (d) as a color-range label only |

## 2. Post-merge draw estimate

Arena today (mesh): 54+37+40+14 = **145 draws / 2130 tris**.
Post-merge arena: (a+d) 1 + (b) 5 + (c) 3 = **9 draws**
(the 4-bucket story holds at the VBO level; texture splits force 9 GL draws —
fixed-function binds one texture per draw, and B2/C5-C6 need their own depth
state; see §5).

Rest of frame (dynamic, NOT merged — counted draws):
Fennec bodies 2 (`NoteExternalDraw`, `Fennec.cpp:259`) + car trim
`DrawBox` 7×2=14 (`Fennec.cpp:109-120`) + pads 34×4=136 (`FX.cpp:132-152`:
trim ring + halo + orb + core when active) + ball indicator 2 + boost/flame,
shockwave, trail, HUD gauge/text ≈ 15-25.
**Projected frame total ≈ 9 + ~170 ≈ ~185, i.e. NOT <60.**

> **FLAG for parent:** the `<60` frame-total goal is unreachable by arena
> merging alone — pads alone are ~136 counted draws. Options: (i) restate G3
> success as *arena* draws 145→9 with frame total ~185; (ii) extend G3 with a
> pad-batching follow-up (static plates → 1 draw; dynamic rings/orbs stay);
> (iii) count car-trim boxes into (a) (saves 14, still ~170). Recommend (i)
> now + (ii) as G3c. Do NOT let G3b silently re-scope into pads/FX.

## 3. Fallback-mode deltas (must stay byte-identical)

- Trim bars: `main.cpp:1298-1307` branch (8 draws, same 96 tris).
- Rounds vanish: B7/C4/B6 zero draws; `chamLen/sideEnd/backEnd` take fallback
  values (`main.cpp:1102-1104`); U fields are fallback-anchored so mesh runs
  are subranges of the same field.
- Cove: 4 straight only (no diagonals); skirt: 8 (no round skirts).
- Fallback arena ≈ 145−(8+16+8)−4−8 = **~101 draws**. Build VBOs per mode at
  startup (mode fixed per run via `sim->usingRealSoccar()`); never mix.

## 4. V5/V6 fix-sensitive draws (must be preserved bit-for-bit by G3b)

- **V5 (lit-from-inside):** ceiling `faceDown=true` → normal −Z
  (`main.cpp:1405-1407`; `Renderer.cpp:499-504`); glass `quadYaw =
  atan2(−n.x, n.y)` inward convention (`main.cpp:1198-1203,1230-1240`);
  corner quad π-rotation to face inward (`main.cpp:1264-1279`); crown radial
  normals, ceiling-course sign (`main.cpp:945-952`); weave yaw per goal
  (`main.cpp:1581-1590`); `GL_NORMALIZE` stays ON (`Renderer.cpp:148`) —
  baked VBO normals must be unit-length; NORMALIZE is then idempotent safety.
- **V6 (420 uu hex period):** all glass/crown/round UVs are arc-length with
  per-axis spans (`spanX=kHexSpanX=2546.1147`, `spanY=2520`,
  `kHexCell=420` in `ArenaTextures.h:104-116`); mouth-split `uOff/vOff`
  sample the wall-global field (`main.cpp:1235-1253`); negative corner
  `uRep` under `GL_REPEAT` (`main.cpp:1272-1279`). **Game UVs never enter
  bucket (b):** turf/curb/hex/weave UVs are our arc-length fields only.
- `--no-hexnet` swaps TEXTURE (`HexNet↔PlainGlass`), never geometry
  (`main.cpp:1218-1219,1275-1276`) — merged VBOs keep UVs; the texture bind
  stays dynamic per draw.

## 5. VBO strategy (4.5-compat, fixed-function)

- Precedent: Fennec VBO/IBO — `glGenBuffers`, `GL_STATIC_DRAW` upload once,
  `glVertexPointer/glNormalPointer/glColorPointer` + `glDrawElements`,
  client-state sandwich (`src/models/Fennec.cpp:152-186,221-229`). Reuse the
  exact pattern; no shaders/VAOs (compat profile keeps fixed-function legal).
- Build-once-at-startup after GL ctx + first texture gen (textures unchanged;
  VBO stores UVs, binds stay per-draw so `--no-hexnet`/team tints still work).
  Bake color (modulate tint) per vertex; keep one texture bind per VBO draw.
- Triangulation must match the driver-visible order: `GL_QUADS (v0,v1,v2,v3)`
  → tris `(v0,v1,v2)+(v0,v2,v3)`; `arcSweep` 12 quads + optional 12-tri cap
  fans (`main.cpp:933-1001`); `planRound` 8 faceted quads (`main.cpp:1043-1058`).
  Emit in today's draw order: (a+d) → (b minus B2) → B2 overlay → (c minus
  interiors) → C5+C6 with `glDepthMask(GL_FALSE)` restored after
  (`main.cpp:1568-1592`). B2 keeps depth-test OFF (`Renderer.cpp:544-562`).
- Counters: each merged draw calls `NoteExternalDraw(sumTris)` with the EXACT
  sums in §1, or VG7 `tris_last` shifts. Gaps G3b should NOT fix in passing
  (note only): `DrawBall` (`Models.cpp:242-274`) and wheel/plate raw GL emit
  tris with no `NoteExternalDraw` — today uncounted; leave as-is for VG7 parity.
- Instruments excluded: `DrawGroundGrid` (`main.cpp:887-888`, `--debug-grid`
  only, off in gates), `--debug-colliders` wire (`main.cpp:1594-1621`).

## 6. VG1 re-baseline risk note

Byte-stable EXPECTED (same verts, same order, same state sandwiches), but
these can shift pixels: float64→float32 intermediates when baking (keep the
`sinf/cosf` float32 chain); quad→triangle diagonal choice (use §5 mapping);
`GL_REPEAT` negative-U wrap on merged corner ranges (keep per-vertex UVs, do
not rebase); blend off-by-one on `a=0.9/0.95` boxes if merged color rounds;
overlay/interior depth-state boundaries (one extra or one missing
`glDepthMask/glDisable(DEPTH_TEST)` flips whole regions); `--no-hexnet` and
fallback/mesh VBO selection (wrong VBO = totally different frame). G3b proves
with VG1 byte-compare + VG7 `tris_last` exact-match + `draws_last` drop only.
