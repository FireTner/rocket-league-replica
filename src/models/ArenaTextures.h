// ArenaTextures.h — procedural arena surface textures.
#pragma once

namespace rl {

// Lazily built, cached texture ids. Each Build*() generates the image on first
// call, uploads it once and returns the cached GL texture name thereafter
// (mirrors BuildBallTexture() in Models.cpp). Safe to call per frame.
//
// Sizes are powers of two for GL 2.1 compatibility (no NPOT extension is
// required). All are GL_RGB / GL_UNSIGNED_BYTE except the glass alpha sheet.

// Mowed turf: 16 mowed bands matching the physics-visible 640 uu stripe pitch,
// with a subtle grass grain so the floor is not a flat fill. 512x512, tileable.
unsigned TurfTexture();

// Turf detail overlay: the painted markings layer (midfield line + centre
// circle) on a transparent sheet, drawn as one quad over the turf. 512x512.
unsigned TurfMarkingsTexture();

// Wall / hex-net glass: the structural hexagon net over a cool blue tint,
// generated at the real 420 uu cell pitch. 512x512, tileable in both axes.
unsigned HexNetTexture();

// Same base as HexNetTexture, with no net. Used by --no-hexnet.
unsigned PlainGlassTexture();

// Goal mouth: the dark interior panel with a team-coloured weave. 256x256.
unsigned GoalInteriorTexture();

// Wall-base fillet: the curved floor->wall ramp surface (RAMP_RADIUS 256).
// 256x256 concrete sheet, arc-length mapped by the draw sites.
unsigned WallBaseTexture();

// Ceiling: base panel + panel seams matching the beam lattice. 256x256.
unsigned CeilingTexture();

// Release every cached texture (call at shutdown; GL context must be live).
void FreeArenaTextures();

// Pixel buffers. The GL uploads above call these. Fill* writes w*h*3, or *4 for markings.
namespace texturegen {

// Sheet dimensions, exposed so tests and draw sites agree on the spans.
constexpr int kTurfW = 512, kTurfH = 512;
constexpr int kMarkW = 512, kMarkH = 512;
constexpr int kHexW = 512, kHexH = 512;
constexpr int kGoalW = 256, kGoalH = 256;
constexpr int kCeilW = 256, kCeilH = 256;
constexpr int kBaseW = 256, kBaseH = 256;

// uu covered by one sheet repeat, per axis. Draw sites divide surface size by
// these to keep texel density identical on every surface.
constexpr float kTurfSpanX = 8192.0f;   // uu, X extent of the floor
constexpr float kTurfSpanY = 10240.0f;  // uu, Y extent of the floor
constexpr float kCeilSpan = 2048.0f;    // uu, one ceiling sheet repeat

// ---- shell courses: where flat glass ends and the curved fillets begin ----
// The walls are not full-height quads. The shell is three courses, all using
// R = 256:
//   floor fillet  z 0..256    (quarter arc, tangent to floor and wall)
//   flat glass    z 256..1792 (the only part that carries the flat net)
//   ceiling fillet z 1792..2048 (quarter arc, net flows around it, arc-length)
// Wall z runs from R to CEILING_Z - R. Draw sites share these so the net stays on flat glass.
constexpr float kFilletR = 256.0f;    // uu, both fillet radii
constexpr float kGlassZ0 = 256.0f;    // uu, flat glass bottom (top of base)
constexpr float kGlassZ1 = 1792.0f;   // uu, flat glass top (= CEILING_Z - R)
// Wall-base sheet span: one sheet covers this many uu along the tangent, and
// exactly one quarter-arc (pi/2 * R) across it, so texel density is uniform.
constexpr float kBaseSpanU = 1024.0f; // uu per sheet along the tangent

// Flat-top hexagon. Cell is vertex-to-vertex.
//   circumradius R    = cell/2
//   horizontal period = sqrt(3)*R   (0.866 * 420 = 363.73 uu)
//   row pitch         = 1.5*R
// A sheet only tiles seamlessly if it holds a WHOLE number of horizontal
// periods and an EVEN number of rows (rows alternate their half-period offset).
// Two further constraints pin the counts down:
//   * the texture is power-of-two (GL 2.1, no NPOT extension), and
//   * hexagons must stay ROUND, i.e. texels-per-uu equal on both axes.
// sqrt(3) is irrational so those cannot all hold exactly; 7 cols x 8 rows is
// the smallest even-row tiling whose world span (2546.11 x 2520 uu) is nearly
// square, leaving only a 1.03% density difference — far below what is visible.
constexpr float kHexCell = 420.0f;   // uu, cell vertex-to-vertex
constexpr int   kHexCols = 7;        // horizontal periods per sheet
constexpr int   kHexRows = 8;        // rows per sheet (even: complete pairs)
// Derived: width 7*sqrt(3)*210 = 2546.1147 uu, height 8*1.5*210 = 2520 uu.
constexpr float kHexSpanX = 2546.1147f;
constexpr float kHexSpanY = 2520.0f;
// Back-compat alias: the height span, which the vertical draw-site math uses.
constexpr float kHexSpan = kHexSpanY;

// Fill a caller-provided buffer. These are the exact functions the GL builders
// call, so a test that asserts on their output asserts on the uploaded bytes.
void FillTurf(unsigned char* rgb);                 // kTurfW*kTurfH*3

// Stadium lamps on the clean-room shell (presentation only). Centres sit
// just outside the glass so they read as bowl lights and never enter the
// play volume. Colours are applied at the draw site and are UNVERIFIED.
struct FieldLamp { float x, y, z; };
constexpr int kFieldLampCount = 16;
constexpr FieldLamp kFieldLamps[kFieldLampCount] = {
    { 4180.0f, -3200.0f, 1680.0f}, { 4180.0f, -1600.0f, 1680.0f},
    { 4180.0f,     0.0f, 1680.0f}, { 4180.0f,  1600.0f, 1680.0f},
    { 4180.0f,  3200.0f, 1680.0f},
    {-4180.0f, -3200.0f, 1680.0f}, {-4180.0f, -1600.0f, 1680.0f},
    {-4180.0f,     0.0f, 1680.0f}, {-4180.0f,  1600.0f, 1680.0f},
    {-4180.0f,  3200.0f, 1680.0f},
    {-1800.0f,  5220.0f, 1680.0f}, {    0.0f,  5220.0f, 1680.0f},
    { 1800.0f,  5220.0f, 1680.0f},
    {-1800.0f, -5220.0f, 1680.0f}, {    0.0f, -5220.0f, 1680.0f},
    { 1800.0f, -5220.0f, 1680.0f},
};
// Housing and bulb extents (uu). Two boxes per lamp, fixed-function.
constexpr float kLampHousingX = 36.0f, kLampHousingY = 18.0f, kLampHousingZ = 18.0f;
constexpr float kLampBulbX = 16.0f, kLampBulbY = 8.0f, kLampBulbZ = 10.0f;
void FillTurfMarkings(unsigned char* rgba);        // kMarkW*kMarkH*4
void FillHexNet(unsigned char* rgb);               // kHexW*kHexH*3
void FillPlainGlass(unsigned char* rgb);           // kHexW*kHexH*3 (no net)
void FillGoalInterior(unsigned char* rgb);         // kGoalW*kGoalH*3
void FillCeiling(unsigned char* rgb);              // kCeilW*kCeilH*3
void FillWallBase(unsigned char* rgb);             // kBaseW*kBaseH*3

} // namespace texturegen

} // namespace rl
