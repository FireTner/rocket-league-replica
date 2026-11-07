// ArenaRip.h — reads an rl-arena-pack directory into meshes, materials, and RGBA8.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rl {

// How a texture's payload was stored in the pack.  The payload of a
// compressed texture is decoded on the CPU into `rgba` (always RGBA8, row
// major, 4 bytes per texel, top-left origin) AND kept in `blocks` so the GL
// upload can hand the driver the original S3TC blocks.  The decode stays:
// the headless contract test compares `rgba` to tools/arena/rldds.py.
enum class ArenaTexFormat : int {
  kRgba8 = 0,  // the pack stored the expanded RGBA8 bytes (storage = "rgba8")
  kDxt1 = 1,   // S3TC BC1 — 8 B per 4x4 block, 3 or 4 colours + 1-bit alpha
  kDxt5 = 2,   // S3TC BC3 — 16 B per 4x4 block, interpolated alpha + 4 colours
};

// One diffuse texture of the pack.
//
// `format` is the ORIGINAL storage format of the payload (the pack keeps the
// compressed form); `rgba` always holds the DECODED pixels, exactly
// `width * height * 4` bytes.  `blocks` is the original compressed payload
// (empty when storage is already RGBA8).  `valid` is true when the payload
// was present, had the expected byte length and decoded without error.
struct ArenaTexture {
  int width = 0, height = 0;
  ArenaTexFormat format = ArenaTexFormat::kRgba8;
  std::vector<uint8_t> rgba;    // decoded, width*height*4, always RGBA8
  std::vector<uint8_t> blocks;  // original DXT1/DXT5 bytes; empty for rgba8
  bool valid = false;
  std::string name;
  bool lightmap = false;  // /2: NormalizedAverage bake (not a diffuse)
};

// One section of a mesh: a run of triangles in the mesh's index buffer that
// shares one material.  `materialSlot` is the section's index into the
// instance's material slot list (see ArenaInstance::materialSlots); it is
// -1 when the mesh carries no sections.
struct ArenaSection {
  uint32_t firstIndex = 0, numTriangles = 0;
  int materialSlot = -1;
};

// One mesh (a `StaticMesh` export of the ripped package, or one of the meshes
// the package only imports).  All vectors are in the mesh's LOCAL space.
//
//   positions : 3 floats per vertex
//   normals   : 3 floats per vertex
//   tangents  : 3 floats per vertex (the pack's `tangents` array)
//   uvs       : 2 floats per vertex (diffuse channel)
//   colors    : 3 floats per vertex, only when the pack stores a `colors`
//               array. This cook's vertex stream is packed tangent + packed
//               normal + UV channels (stride 8+4*channels). It has no colour,
//               and the stadium pack does not either — `colors` stays empty.
//   indices   : triangle-corner indices, 3 per triangle
struct ArenaMesh {
  std::string name;
  std::vector<float> positions;  // 3 per vertex
  std::vector<float> normals;    // 3 per vertex
  std::vector<float> tangents;   // 3 per vertex (empty if the pack omits them)
  std::vector<float> uvs;        // 2 per vertex (diffuse channel)
  std::vector<float> uvsLm;      // 2 per vertex (lightmap channel; /2)
  std::vector<float> colors;     // 3 per vertex, only when a `colors` array exists
  int diffuseUvChannel = 0;
  int lightmapUvChannel = 0;
  std::vector<uint16_t> indices;
  std::vector<ArenaSection> sections;
};

// One placement of a mesh in the world.
//
// `matrix` is COLUMN-major (m[0..3] is the image of local +X, m[12..14] is
// the translation, m[15] == 1), matching the pack's own convention.  The pack
// stores 16 doubles; this reader narrows them to float (the render path's
// precision), which is lossless enough for the arena's coordinate range.
//
// `materialSlots` has one entry per section of the placed mesh: the index of
// the bound material in ArenaPack::materials, or -1 when the slot is empty.
struct ArenaLightmap {
  int texture = -1;                   // index into ArenaPack::textures
  float coordScale[2] = {1.f, 1.f};
  float coordBias[2] = {0.f, 0.f};
  float colorScale[3] = {1.f, 1.f, 1.f};
  bool valid = false;
};

struct ArenaInstance {
  std::string mesh;
  std::string owner;                  // owning actor (e.g. DecalBlockingActor_TA
                                      // placements are invisible gameplay helpers)
  float matrix[16]{};                 // COLUMN-major, m[12..14]=translation
  std::vector<int> materialSlots;     // per section slot -> material index
  ArenaLightmap lightmap;             // /2: baked NormalizedAverage binding
};

// One material of the pack's material table.  `texture` is the index into
// ArenaPack::textures of the material's diffuse texture, or -1 when the
// material owns no diffuse (the majority: 331 of 640 used section slots are
// "untextured by design").
struct ArenaMaterial {
  std::string name;
  int texture = -1;
};

// The pack holds two mesh tables and two instance tables.
// meshes/instances are the package's own; imports live in the external* vectors.
struct ArenaPack {
  std::string format;
  int formatVersion = 0;
  bool hasLightmaps = false;  // /2 pack with uvs_lm + instance lightmap rows
  std::string sourcePackage, sourceGuid;
  std::vector<ArenaMesh> meshes;
  // name -> index into `meshes`.  A mesh NAME is not unique in a UE3 package
  // (the DFH Stadium day pack holds eight exports called "Grass"), so this
  // map holds the FIRST mesh of a name; every mesh stays in `meshes` and is
  // reachable by index, and `duplicateMeshNames` counts the names that more
  // than one mesh carries.
  std::map<std::string, uint32_t> meshIndexByName;
  std::vector<ArenaInstance> instances;
  std::vector<ArenaMaterial> materials;
  std::vector<ArenaTexture> textures;
  std::string error;

  std::vector<ArenaMesh> externalMeshes;      // meshes the package imports
  std::vector<ArenaInstance> externalInstances;  // placements of those meshes
  std::map<std::string, uint32_t> externalMeshIndexByName;
  bool blobSha256Checked = false;   // the manifest recorded a blob sha256
  std::string blobSha256Expected;   // ... what the manifest says
  std::string blobSha256Actual;     // ... what the reader computed
  uint32_t sectionCount = 0;        // sections over meshes + externalMeshes
  uint32_t usedSectionSlots = 0;    // section slots over the placements
  uint32_t duplicateMeshNames = 0;  // meshes whose name an earlier mesh took
};

// Load the pack in `dir` (the directory that holds `arena.json` + `arena.bin`).
//
// Returns true on success.  On failure it returns false and fills
// `out.error` with a message that says WHAT is wrong (and, for a missing
// pack, WHICH file is expected).  Every failure is loud: there is no silent
// fallback and no partial pack.
//
// Checks performed (in this order — the first one that fails is the one
// reported):
//   1. the manifest `arena.json` exists and parses as a JSON object
//   2. `format` == "rl-arena-pack/1" and `format_version` == 1
//   3. the blob `arena.bin` exists
//   4. the blob's size matches the manifest's `blob.bytes`
//   5. the blob's sha256 matches the manifest's `blob.sha256` (when recorded)
//   6. the blob header (magic, version, header size, total size, array count)
//   7. every texture: byte range inside the blob, byte length == the DXT size
//      for its dimensions, payload hash, and a successful decode to w*h*4
//   8. every mesh: array byte ranges inside the blob, vertex/index counts
//      agree, every index < the vertex count, every section's index range
//      inside the index buffer, no NaN/Inf in a vertex
//   9. every instance: the matrix's byte range inside the blob, a finite
//      matrix, and a mesh name that the pack knows
bool LoadArenaPack(const std::string& dir, ArenaPack& out);

// World-space axis-aligned bounds of one placement: every vertex of the
// placed mesh transformed by the instance's matrix.  Returns false when the
// instance names no mesh the pack knows (or the mesh has no vertices), and
// leaves `lo` / `hi` untouched.
bool ComputeInstanceAabb(const ArenaPack& p, const ArenaInstance& inst,
                         float lo[3], float hi[3]);

// The error message of a failed load ("" when the pack loaded fine).
const char* ArenaPackError(const ArenaPack& p);

// Own meshes first, then imports. Returns nullptr when the name is unknown.
const ArenaMesh* FindArenaMesh(const ArenaPack& p, const std::string& name);

}  // namespace rl
