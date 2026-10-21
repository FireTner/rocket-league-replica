// ArenaRip.h — the arena-pack reader (phase A6a of the arena 1:1 order).
//
// Reads a *ripped arena pack* (`rl-arena-pack/1`, see docs/ARENA_PACK.md) into
// memory: the manifest, the binary blob, the mesh / instance / material /
// texture tables, and the CPU-decoded RGBA8 pixels of every diffuse texture.
//
// The pack is Psyonix-owned data.  It is a RUNTIME INPUT: it lives under the
// gitignored `arena_assets/` directory (or anywhere outside the repository) and
// is never committed (AGENTS.md rule 1).  A missing pack is a normal, clean
// state — callers must handle a false return and read ArenaPackError().
//
// Ownership: `src/models` is presentation (CONTRACTS.md v2).  This file is
// deliberately free of GL, SDL and windowing: the reader is plain C++ + STL so
// it is testable headless (phase A6a's contract test) and so the GL side
// (phase A6b) can be a thin upload layer.
//
// No GL calls, no global state, no file writes: LoadArenaPack() only reads.
// Determinism: the same pack always loads to the same tables (the pack itself
// is byte-deterministic; the reader preserves the manifest's table order and
// never sorts anything the manifest already ordered).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rl {

// How a texture's payload was stored in the pack.  The payload of a
// compressed texture is decoded on the CPU into `rgba` (always RGBA8, row
// major, 4 bytes per texel, top-left origin).
enum class ArenaTexFormat : int {
  kRgba8 = 0,  // the pack stored the expanded RGBA8 bytes (storage = "rgba8")
  kDxt1 = 1,   // S3TC BC1 — 8 B per 4x4 block, 3 or 4 colours + 1-bit alpha
  kDxt5 = 2,   // S3TC BC3 — 16 B per 4x4 block, interpolated alpha + 4 colours
};

// One diffuse texture of the pack.
//
// `format` is the ORIGINAL storage format of the payload (the pack keeps the
// compressed form); `rgba` always holds the DECODED pixels, exactly
// `width * height * 4` bytes.  `valid` is true when the payload was present,
// had the expected byte length and decoded without error.
struct ArenaTexture {
  int width = 0, height = 0;
  ArenaTexFormat format = ArenaTexFormat::kRgba8;
  std::vector<uint8_t> rgba;  // decoded, width*height*4, always RGBA8
  bool valid = false;
  std::string name;  // A6a addition: the texture name (diagnostics / lookup)
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
//   uvs       : 2 floats per vertex (UV channel 0 — the only stored channel)
//   indices   : triangle-corner indices, 3 per triangle
struct ArenaMesh {
  std::string name;
  std::vector<float> positions;  // 3 per vertex
  std::vector<float> normals;    // 3 per vertex
  std::vector<float> uvs;        // 2 per vertex
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
struct ArenaInstance {
  std::string mesh;
  std::string owner;                  // owning actor (e.g. DecalBlockingActor_TA
                                      // placements are invisible gameplay helpers)
  float matrix[16]{};                 // COLUMN-major, m[12..14]=translation
  std::vector<int> materialSlots;     // per section slot -> material index
};

// One material of the pack's material table.  `texture` is the index into
// ArenaPack::textures of the material's diffuse texture, or -1 when the
// material owns no diffuse (the majority: 331 of 640 used section slots are
// "untextured by design").
struct ArenaMaterial {
  std::string name;
  int texture = -1;
};

// The whole pack.
//
// The five vectors below are the contract of phase A6a; the members after
// `error` are A6a additions (documented in the A6a round report) that the
// contract's single-list fields could not express:
//
//   * the pack holds TWO mesh tables (the package's own exports and the meshes
//     it only imports) and TWO instance tables.  `meshes` / `instances` are the
//     package's own (119 / 451 for the DFH Stadium day pack); the imported
//     ones live in `externalMeshes` / `externalInstances` (4 / 26) so the
//     primary counts stay exactly what the manifest counts.
//   * `blobSha256*` report whether the manifest recorded the blob's hash and
//     what the reader computed for it.
struct ArenaPack {
  std::string format;
  int formatVersion = 0;
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

  // --- A6a additions (additive only; the contract above is unchanged) -------
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

// A6a addition: find a mesh by name in either mesh table (the package's own
// first, then the imported ones).  Returns nullptr when the name is unknown.
const ArenaMesh* FindArenaMesh(const ArenaPack& p, const std::string& name);

}  // namespace rl
