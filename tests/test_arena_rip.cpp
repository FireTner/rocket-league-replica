// tests/test_arena_rip.cpp — the contract test for the arena-pack reader (A6a).
//
// What it proves:
//   * the pack at arena_assets/stadium_day loads into the documented tables
//     (119 + 4 meshes, 451 + 26 placements, 178 sections, 83 materials,
//     13 textures),
//   * every texture decodes on the CPU to exactly w*h*4 RGBA8 bytes,
//   * every mesh is internally consistent (vertex/uv/index counts, every
//     index inside the vertex range) and every placement is finite and names
//     a mesh the pack knows (0 orphans),
//   * the world-space AABBs of the field meshes hit the arena oracle
//     (field walls |x| ~ 4096, goal mouth +-2688, centre circle r ~ 707,
//     the DFH base spanning +-31000),
//   * a bad directory fails LOUDLY (false + a message that names the expected
//     manifest) and never crashes,
//   * the CPU DXT decoder is byte-identical to the Python reference decoder
//     (tools/arena/rldds.py) whenever its output dump is present.
//
// The pack is Psyonix-owned data and a RUNTIME INPUT: it lives in the
// gitignored arena_assets/ and is never copied into the repository.  A
// machine without the pack SKIPS loudly and stays green — a missing pack
// must never fail the suite.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "models/ArenaRip.h"

using namespace rl;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); \
           std::printf("\n"); } \
} while (0)

namespace fs = std::filesystem;

// The oracle of the arena geometry (docs/ARENA_RIP_LOG.md round A4 §9 and
// tools/arena/rlmesh.py ORACLE): the measured world-space bounds of the five
// oracle meshes.
struct Aabb {
  bool valid = false;
  double lo[3] = {0, 0, 0};
  double hi[3] = {0, 0, 0};
};

// The union of the world-space AABBs of every placement of `meshName`.
static Aabb UnionForMesh(const ArenaPack& p, const std::string& meshName) {
  Aabb out;
  const auto add = [&](const ArenaInstance& inst) {
    float lo[3], hi[3];
    if (!ComputeInstanceAabb(p, inst, lo, hi)) return;
    if (!out.valid) {
      out.valid = true;
      for (int c = 0; c < 3; ++c) {
        out.lo[c] = lo[c];
        out.hi[c] = hi[c];
      }
      return;
    }
    for (int c = 0; c < 3; ++c) {
      if (lo[c] < out.lo[c]) out.lo[c] = lo[c];
      if (hi[c] > out.hi[c]) out.hi[c] = hi[c];
    }
  };
  for (const ArenaInstance& inst : p.instances)
    if (inst.mesh == meshName) add(inst);
  for (const ArenaInstance& inst : p.externalInstances)
    if (inst.mesh == meshName) add(inst);
  return out;
}

static void PrintAabb(const char* label, const Aabb& b) {
  if (!b.valid) {
    std::printf("  %-30s (no placement)\n", label);
    return;
  }
  std::printf("  %-30s x[%10.3f, %10.3f] y[%10.3f, %10.3f] z[%10.3f, %10.3f]\n",
              label, b.lo[0], b.hi[0], b.lo[1], b.hi[1], b.lo[2], b.hi[2]);
}

// ---- the Python reference cross-check -------------------------------------
// The round-A3 dump of the Python decoder's output (tools/arena/rldds.py).
// Its files are named "<package>_<texture>_e<export>_<w>x<h>_<format>.rgba",
// where <format> is a UE3 pixel format ("PF_DXT1", "PF_DXT5", ...).
static bool ParsePythonTextureName(const std::string& file, std::string& name,
                                   int& w, int& h, std::string& format) {
  const std::string suffix = ".rgba";
  if (file.size() <= suffix.size() ||
      file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0)
    return false;
  const std::string stem = file.substr(0, file.size() - suffix.size());
  // the format: everything from the last "_PF_"
  const size_t pf = stem.rfind("_PF_");
  if (pf == std::string::npos) return false;
  format = stem.substr(pf + 1);
  const std::string head = stem.substr(0, pf);  // "<name>_e<export>_<w>x<h>"
  const size_t sizeSep = head.rfind('_');
  if (sizeSep == std::string::npos) return false;
  const std::string sizePart = head.substr(sizeSep + 1);  // "<w>x<h>"
  const size_t x = sizePart.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(sizePart.substr(0, x).c_str());
  h = std::atoi(sizePart.substr(x + 1).c_str());
  const std::string head2 = head.substr(0, sizeSep);  // "<name>_e<export>"
  const size_t e = head2.rfind("_e");
  if (e == std::string::npos) return false;
  bool digits = e + 2 < head2.size();
  for (size_t i = e + 2; i < head2.size() && digits; ++i)
    digits = head2[i] >= '0' && head2[i] <= '9';
  if (!digits) return false;
  name = head2.substr(0, e);
  return w > 0 && h > 0;
}

static bool ReadBytes(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

int main(int argc, char** argv) {
  // ---- where is the pack? ------------------------------------------------
  const char* envDir = std::getenv("ARENA_PACK_DIR");
  std::string packDir = envDir ? envDir : "arena_assets/stadium_day";
  if (argc > 1) packDir = argv[1];

  if (!fs::exists(fs::path(packDir) / "arena.json")) {
    std::printf(
        "ARENA_RIP SKIP: no arena pack at '%s' (arena.json is missing).\n"
        "  The pack is Psyonix-owned RUNTIME DATA: it lives in the gitignored\n"
        "  arena_assets/ and must never be committed (AGENTS.md rule 1).\n"
        "  Skipping the loader contract test — a clean-room machine stays green.\n",
        packDir.c_str());
    return 0;
  }

  // ---- 1. the load --------------------------------------------------------
  ArenaPack pack;
  const bool loaded = LoadArenaPack(packDir, pack);
  if (!loaded) {
    std::printf("FAIL: LoadArenaPack('%s') failed: %s\n", packDir.c_str(),
                ArenaPackError(pack));
    ++g_fail;
    std::printf("ARENA_RIP: %d PASS, %d FAIL\n", g_pass, g_fail);
    return 1;
  }
  CHECK(pack.error.empty(), "a successful load left an error: '%s'",
        pack.error.c_str());
  CHECK(pack.format == "rl-arena-pack/1", "format is '%s'",
        pack.format.c_str());
  CHECK(pack.formatVersion == 1, "format_version is %d", pack.formatVersion);
  CHECK(pack.sourcePackage == "Stadium_Day_P", "source package is '%s'",
        pack.sourcePackage.c_str());
  CHECK(pack.sourceGuid == "e8eba3237450a04499cfaaf200572d62",
        "source guid is '%s'", pack.sourceGuid.c_str());
  std::printf("ARENA_RIP: loaded '%s' (format %s v%d, source %s)\n",
              packDir.c_str(), pack.format.c_str(), pack.formatVersion,
              pack.sourcePackage.c_str());
  std::printf("  blob sha256      : checked=%d expected=%s actual=%s\n",
              pack.blobSha256Checked ? 1 : 0,
              pack.blobSha256Expected.empty() ? "(none recorded)"
                                               : pack.blobSha256Expected.c_str(),
              pack.blobSha256Actual.empty() ? "(not computed)"
                                            : pack.blobSha256Actual.c_str());

  // ---- 2. the counts -----------------------------------------------------
  CHECK(pack.meshes.size() == 119, "meshes = %zu (expected 119)",
        pack.meshes.size());
  CHECK(pack.externalMeshes.size() == 4, "external meshes = %zu (expected 4)",
        pack.externalMeshes.size());
  CHECK(pack.instances.size() == 451, "instances = %zu (expected 451)",
        pack.instances.size());
  CHECK(pack.externalInstances.size() == 26,
        "external instances = %zu (expected 26)", pack.externalInstances.size());
  CHECK(pack.sectionCount == 178, "sections = %u (expected 178)",
        pack.sectionCount);
  CHECK(pack.materials.size() == 83, "materials = %zu (expected 83)",
        pack.materials.size());
  CHECK(pack.textures.size() == 13, "textures = %zu (expected 13)",
        pack.textures.size());
  CHECK(pack.usedSectionSlots == 640, "used section slots = %u (expected 640)",
        pack.usedSectionSlots);
  std::printf(
      "  counts           : meshes %zu (+%zu external)  instances %zu (+%zu "
      "external)\n"
      "                     sections %u  materials %zu  textures %zu  used "
      "section slots %u\n",
      pack.meshes.size(), pack.externalMeshes.size(), pack.instances.size(),
      pack.externalInstances.size(), pack.sectionCount, pack.materials.size(),
      pack.textures.size(), pack.usedSectionSlots);

  // ---- 3. the textures decode -------------------------------------------
  uint64_t rgbaTotal = 0;
  int dxt1 = 0, dxt5 = 0, rgba8 = 0;
  for (const ArenaTexture& t : pack.textures) {
    const size_t want = static_cast<size_t>(t.width) *
                        static_cast<size_t>(t.height) * 4u;
    CHECK(t.valid, "texture '%s' is not valid", t.name.c_str());
    CHECK(t.rgba.size() == want, "texture '%s' decoded to %zu bytes (want %zu)",
          t.name.c_str(), t.rgba.size(), want);
    CHECK(t.rgba.size() > 0, "texture '%s' decoded to nothing", t.name.c_str());
    CHECK(t.width > 0 && t.height > 0, "texture '%s' has no size",
          t.name.c_str());
    rgbaTotal += t.rgba.size();
    if (t.format == ArenaTexFormat::kDxt1) ++dxt1;
    if (t.format == ArenaTexFormat::kDxt5) ++dxt5;
    if (t.format == ArenaTexFormat::kRgba8) ++rgba8;
  }
  CHECK(rgbaTotal == 70909952ull, "the decoded pixels are %llu bytes (want %llu)",
        static_cast<unsigned long long>(rgbaTotal), 70909952ull);
  // The pack stores the textures either compressed (the default) or already
  // expanded to RGBA8; both must decode to the same pixels.
  if (rgba8 == 13) {
    std::printf("  textures         : %zu decoded, %llu bytes of RGBA8 "
                "(the pack stores them expanded)\n",
                pack.textures.size(), static_cast<unsigned long long>(rgbaTotal));
  } else {
    CHECK(dxt1 == 8 && dxt5 == 5, "the compressed formats are DXT1=%d DXT5=%d "
          "(want 8/5)", dxt1, dxt5);
    std::printf("  textures         : %zu decoded, %llu bytes of RGBA8 "
                "(DXT1 %d, DXT5 %d)\n",
                pack.textures.size(), static_cast<unsigned long long>(rgbaTotal),
                dxt1, dxt5);
  }

  // ---- 4. the meshes are internally consistent ---------------------------
  size_t totalVerts = 0, totalIndices = 0, totalTris = 0;
  int meshesWithSections = 0;
  const auto checkMesh = [&](const ArenaMesh& m) {
    const size_t verts = m.positions.size() / 3;
    CHECK(m.positions.size() == 3u * verts, "mesh '%s': positions are not "
          "3 per vertex", m.name.c_str());
    CHECK(m.normals.size() == m.positions.size(),
          "mesh '%s': normals (%zu) != positions (%zu)", m.name.c_str(),
          m.normals.size(), m.positions.size());
    CHECK(m.uvs.size() == 2u * verts, "mesh '%s': uvs are not 2 per vertex",
          m.name.c_str());
    CHECK(m.indices.size() % 3 == 0, "mesh '%s': %zu indices is not a multiple "
          "of 3", m.name.c_str(), m.indices.size());
    for (uint16_t idx : m.indices) {
      if (static_cast<size_t>(idx) >= verts) {
        CHECK(false, "mesh '%s': the index %u is outside its %zu vertices",
              m.name.c_str(), static_cast<unsigned>(idx), verts);
        break;
      }
    }
    for (size_t s = 0; s < m.sections.size(); ++s) {
      const ArenaSection& sec = m.sections[s];
      const size_t last = sec.firstIndex + static_cast<size_t>(sec.numTriangles) * 3u;
      if (last > m.indices.size()) {
        CHECK(false, "mesh '%s': section %zu runs past the index buffer",
              m.name.c_str(), s);
        break;
      }
      CHECK(sec.materialSlot == static_cast<int>(s),
            "mesh '%s': section %zu has the material slot %d", m.name.c_str(),
            s, sec.materialSlot);
    }
    totalVerts += verts;
    totalIndices += m.indices.size();
    totalTris += m.indices.size() / 3;
    if (!m.sections.empty()) ++meshesWithSections;
  };
  for (const ArenaMesh& m : pack.meshes) checkMesh(m);
  for (const ArenaMesh& m : pack.externalMeshes) checkMesh(m);
  CHECK(totalVerts == 330187, "the meshes hold %zu vertices (want 330187)",
        totalVerts);
  CHECK(totalIndices == 933633, "the meshes hold %zu indices (want 933633)",
        totalIndices);
  CHECK(meshesWithSections == 123, "%d of 123 meshes carry sections (want 123)",
        meshesWithSections);
  std::printf("  geometry         : %zu vertices, %zu indices, %zu triangles, "
              "%u sections\n",
              totalVerts, totalIndices, totalTris, pack.sectionCount);

  // ---- 5. the placements -------------------------------------------------
  int orphans = 0, nonFinite = 0, slotMismatch = 0;
  const auto checkInstance = [&](const ArenaInstance& inst) {
    const ArenaMesh* mesh = FindArenaMesh(pack, inst.mesh);
    if (!mesh) {
      ++orphans;
      return;
    }
    for (int c = 0; c < 16; ++c) {
      if (!std::isfinite(inst.matrix[c])) {
        ++nonFinite;
        break;
      }
    }
    if (inst.materialSlots.size() != mesh->sections.size()) ++slotMismatch;
  };
  for (const ArenaInstance& inst : pack.instances) checkInstance(inst);
  for (const ArenaInstance& inst : pack.externalInstances) checkInstance(inst);
  CHECK(orphans == 0, "%d placements name an unknown mesh", orphans);
  CHECK(nonFinite == 0, "%d placements hold a non-finite matrix", nonFinite);
  CHECK(slotMismatch == 0,
        "%d placements have a material-slot list that does not match the "
        "section count of their mesh", slotMismatch);

  // The materials that really carry a diffuse texture.
  int materialsWithTexture = 0;
  for (const ArenaMaterial& m : pack.materials)
    if (m.texture >= 0) ++materialsWithTexture;
  // The resolved-diffuse coverage: the A5 round measured 248 of the 640 used
  // section slots (38.75 %) resolving to a decoded diffuse texture.  The
  // loader must reproduce that number from the pack's own tables.
  uint32_t slotsWithTexture = 0;
  const auto countSlots = [&](const ArenaInstance& inst) {
    for (int slot : inst.materialSlots) {
      if (slot >= 0 && slot < static_cast<int>(pack.materials.size()) &&
          pack.materials[slot].texture >= 0)
        ++slotsWithTexture;
    }
  };
  for (const ArenaInstance& inst : pack.instances) countSlots(inst);
  for (const ArenaInstance& inst : pack.externalInstances) countSlots(inst);
  CHECK(materialsWithTexture == 21, "%d materials carry a diffuse texture "
        "(want 21)", materialsWithTexture);
  CHECK(slotsWithTexture == 248, "%u of the %u used section slots resolve to a "
        "diffuse texture (want 248)", slotsWithTexture, pack.usedSectionSlots);
  std::printf("  placements       : %zu + %zu, 0 orphans, %d materials with a "
              "diffuse texture\n",
              pack.instances.size(), pack.externalInstances.size(),
              materialsWithTexture);
  std::printf("  diffuse coverage : %u of %u used section slots (%.2f%%) "
              "resolve to a diffuse texture\n",
              slotsWithTexture, pack.usedSectionSlots,
              pack.usedSectionSlots
                  ? 100.0 * static_cast<double>(slotsWithTexture) /
                        static_cast<double>(pack.usedSectionSlots)
                  : 0.0);
  if (pack.duplicateMeshNames != 0) {
    std::printf("  note             : %u further mesh(es) take a name an "
                "earlier mesh already has (the pack's instance rows name "
                "their mesh, not its export index)\n",
                pack.duplicateMeshNames);
  }

  // ---- 6. the arena oracle (the world-space AABBs) ----------------------
  const Aabb walls = UnionForMesh(pack, "Field_STD_Collision_SideBot");
  const Aabb centre = UnionForMesh(pack, "Field_Center");
  const Aabb goal = UnionForMesh(pack, "Goal_STD_Collision");
  const Aabb base = UnionForMesh(pack, "DFH_Day_StadiumBase2");
  const Aabb corner = UnionForMesh(pack, "Field_STD_Collision_Corner");
  std::printf("  oracle AABBs (world space):\n");
  PrintAabb("Field_STD_Collision_SideBot", walls);
  PrintAabb("Field_STD_Collision_Corner", corner);
  PrintAabb("Field_Center", centre);
  PrintAabb("Goal_STD_Collision", goal);
  PrintAabb("DFH_Day_StadiumBase2", base);

  CHECK(walls.valid, "no placement carries the field walls");
  if (walls.valid) {
    CHECK(std::fabs(walls.lo[0] + 4106.0) < 2.0,
          "the field walls start at x=%.3f (want -4106)", walls.lo[0]);
    CHECK(std::fabs(walls.hi[0] - 4106.0) < 2.0,
          "the field walls end at x=%.3f (want +4106)", walls.hi[0]);
    CHECK(std::fabs(walls.lo[1] + 3840.0) < 2.0 && std::fabs(walls.hi[1] - 3840.0) < 2.0,
          "the field walls span y[%.3f, %.3f] (want +-3840)", walls.lo[1],
          walls.hi[1]);
    // The arena truth is a half-length of 4096; the measured mesh is 4106.
    CHECK(std::fabs(walls.hi[0] - 4096.0) / 4096.0 < 0.005,
          "the field walls sit at |x|=%.3f, which is not within 0.5%% of the "
          "arena truth 4096", walls.hi[0]);
  }
  CHECK(centre.valid, "no placement carries the centre circle");
  if (centre.valid) {
    const double r = std::max(std::max(-centre.lo[0], centre.hi[0]),
                              std::max(-centre.lo[1], centre.hi[1]));
    CHECK(std::fabs(r - 707.0) < 1.5, "the centre circle has r=%.3f (want 707)",
          r);
    CHECK(std::fabs(centre.lo[2]) < 0.01 && std::fabs(centre.hi[2]) < 0.01,
          "the centre circle is at z[%.4f, %.4f] (want 0)", centre.lo[2],
          centre.hi[2]);
  }
  CHECK(goal.valid, "no placement carries the goal collision mesh");
  if (goal.valid) {
    CHECK(std::fabs(goal.lo[0] + 2688.0) < 0.5,
          "the goal mouth starts at x=%.3f (want -2688)", goal.lo[0]);
    CHECK(std::fabs(goal.hi[0] - 2688.0) < 0.5,
          "the goal mouth ends at x=%.3f (want +2688)", goal.hi[0]);
  }
  CHECK(base.valid, "no placement carries the stadium base");
  if (base.valid) {
    CHECK(std::fabs(base.lo[0] + 31073.0) < 2.0,
          "the stadium base starts at x=%.3f (want -31073)", base.lo[0]);
    CHECK(std::fabs(base.hi[0] - 27472.0) < 2.0,
          "the stadium base ends at x=%.3f (want 27472)", base.hi[0]);
  }

  // ---- 7. the load is deterministic --------------------------------------
  {
    ArenaPack again;
    CHECK(LoadArenaPack(packDir, again), "the second load failed: %s",
          ArenaPackError(again));
    CHECK(again.meshes.size() == pack.meshes.size() &&
              again.instances.size() == pack.instances.size() &&
              again.textures.size() == pack.textures.size(),
          "the second load has different counts");
    bool same = true;
    for (size_t i = 0; i < pack.meshes.size() && same; ++i) {
      same = pack.meshes[i].name == again.meshes[i].name &&
             pack.meshes[i].positions.size() == again.meshes[i].positions.size();
      for (size_t v = 0; v < pack.meshes[i].positions.size() && same; ++v)
        same = pack.meshes[i].positions[v] == again.meshes[i].positions[v];
    }
    for (size_t i = 0; i < pack.instances.size() && same; ++i) {
      same = pack.instances[i].mesh == again.instances[i].mesh;
      for (int c = 0; c < 16 && same; ++c)
        same = pack.instances[i].matrix[c] == again.instances[i].matrix[c];
    }
    for (size_t i = 0; i < pack.textures.size() && same; ++i)
      same = pack.textures[i].rgba == again.textures[i].rgba;
    CHECK(same, "two loads of the same pack differ");
  }

  // ---- 8. a bad directory fails loudly -----------------------------------
  {
    ArenaPack bad;
    const bool ok = LoadArenaPack("/tmp/does-not-exist-arena", bad);
    CHECK(!ok, "a missing directory was accepted");
    CHECK(!bad.error.empty(), "a missing directory produced no error message");
    CHECK(bad.error.find("arena.json") != std::string::npos,
          "the error does not name the expected manifest: '%s'",
          bad.error.c_str());
    std::printf("  bad dir          : %s\n", bad.error.c_str());
  }
  {
    // A directory with a garbage manifest.
    const fs::path dir("/tmp/rl_a6a_garbage");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    { std::ofstream o(dir / "arena.json"); o << "this is not json at all {{{"; }
    ArenaPack bad;
    const bool ok = LoadArenaPack(dir.string(), bad);
    CHECK(!ok, "a garbage manifest was accepted");
    CHECK(!bad.error.empty(), "a garbage manifest produced no error message");
    std::printf("  garbage manifest : %s\n", bad.error.c_str());
  }
  {
    // A valid manifest without the blob.
    const fs::path dir("/tmp/rl_a6a_noblob");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    { std::ofstream o(dir / "arena.json");
      o << "{\"format\": \"rl-arena-pack/1\", \"format_version\": 1, "
           "\"blob\": {\"file\": \"arena.bin\", \"bytes\": 32}}"; }
    ArenaPack bad;
    const bool ok = LoadArenaPack(dir.string(), bad);
    CHECK(!ok, "a manifest without a blob was accepted");
    CHECK(bad.error.find("arena.bin") != std::string::npos,
          "the error does not name the missing blob: '%s'", bad.error.c_str());
    std::printf("  missing blob     : %s\n", bad.error.c_str());
  }
  {
    // A JSON object that is not an arena pack at all.
    const fs::path dir("/tmp/rl_a6a_wrongformat");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    { std::ofstream o(dir / "arena.json"); o << "{\"format\": \"not-a-pack/9\"}"; }
    ArenaPack bad;
    const bool ok = LoadArenaPack(dir.string(), bad);
    CHECK(!ok, "a foreign manifest was accepted");
    CHECK(bad.error.find("not-a-pack/9") != std::string::npos,
          "the error does not name the foreign format: '%s'", bad.error.c_str());
    std::printf("  wrong format     : %s\n", bad.error.c_str());
  }

  // ---- 8b. malformed manifests must never crash the reader ---------------
  {
    std::vector<std::pair<std::string, std::string>> cases;
    cases.emplace_back("truncated json",
                       "{\"format\": \"rl-arena-pack/1\", \"format_version\": 1");
    cases.emplace_back("trailing comma",
                       "{\"format\": \"rl-arena-pack/1\",}");
    cases.emplace_back("not an object", "[1, 2, 3]");
    cases.emplace_back("bare scalar", "42");
    cases.emplace_back("bad escape", "{\"format\": \"a\\qb\"}");
    cases.emplace_back("control char in a string",
                       std::string("{\"format\": \"a\tb\"}"));
    cases.emplace_back("huge number", "{\"format_version\": 1e999}");
    cases.emplace_back("leading zero", "{\"format_version\": 01}");
    cases.emplace_back("unterminated string", "{\"format\": \"abc");
    cases.emplace_back("binary garbage",
                       std::string("\x00\x01\x02\xff\xfe", 5));
    {
      // 400 nested arrays — deeper than the reader's depth cap.
      std::string deep;
      for (int i = 0; i < 400; ++i) deep += "[";
      for (int i = 0; i < 400; ++i) deep += "]";
      cases.emplace_back("too deeply nested", deep);
    }
    const fs::path dir("/tmp/rl_a6a_malformed");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    for (const auto& c : cases) {
      { std::ofstream o(dir / "arena.json", std::ios::binary); o << c.second; }
      ArenaPack bad;
      const bool ok = LoadArenaPack(dir.string(), bad);
      CHECK(!ok, "the malformed manifest '%s' was accepted", c.first.c_str());
      CHECK(!bad.error.empty(), "the malformed manifest '%s' produced no error",
            c.first.c_str());
    }
    // A manifest whose declared blob size does not match the file.
    { std::ofstream o(dir / "arena.json");
      o << "{\"format\": \"rl-arena-pack/1\", \"format_version\": 1, "
           "\"blob\": {\"file\": \"arena.bin\", \"bytes\": 999999}}"; }
    { std::ofstream o(dir / "arena.bin", std::ios::binary);
      o << std::string(32, '\0'); }
    ArenaPack bad;
    CHECK(!LoadArenaPack(dir.string(), bad),
          "a manifest with a wrong blob size was accepted");
    CHECK(bad.error.find("999999") != std::string::npos,
          "the error does not name the declared blob size: '%s'",
          bad.error.c_str());
    std::printf("  malformed input  : %zu malformed manifests rejected, none "
                "crashed\n", cases.size());
  }

  // ---- 9. the DXT decoder vs the Python reference ------------------------
  {
    const char* envTex = std::getenv("ARENA_PY_TEX_DIR");
    const fs::path pyDir(envTex ? envTex : "/tmp/rl_a3/tex");
    if (!fs::exists(pyDir)) {
      std::printf(
          "  DXT cross-check  : SKIP — the Python reference dump '%s' is "
          "absent (it is the round-A3 output of tools/arena/rldds.py, not a "
          "repository file)\n",
          pyDir.string().c_str());
    } else {
      int compared = 0, matched = 0;
      std::vector<std::string> unmatched;
      for (const ArenaTexture& t : pack.textures) {
        bool found = false;
        for (const fs::directory_entry& e : fs::directory_iterator(pyDir)) {
          if (!e.is_regular_file()) continue;
          std::string name;
          int w = 0, h = 0;
          std::string format;
          if (!ParsePythonTextureName(e.path().filename().string(), name, w, h,
                                      format))
            continue;
          if (w != t.width || h != t.height) continue;
          if (t.format == ArenaTexFormat::kDxt1) {
            if (format != "PF_DXT1") continue;
          } else if (t.format == ArenaTexFormat::kDxt5) {
            if (format != "PF_DXT5") continue;
          }
          // A pack that stores the textures already expanded (storage =
          // "rgba8") holds exactly the pixels the Python decoder produced,
          // so the reference file may carry any of the source formats.
          const bool nameMatches =
              name == t.name ||
              (name.size() > t.name.size() &&
               name.compare(name.size() - t.name.size() - 1, t.name.size() + 1,
                            "_" + t.name) == 0);
          if (!nameMatches) continue;
          std::vector<uint8_t> reference;
          if (!ReadBytes(e.path(), reference)) continue;
          found = true;
          ++compared;
          if (reference.size() == t.rgba.size() &&
              std::memcmp(reference.data(), t.rgba.data(), reference.size()) == 0) {
            ++matched;
            std::printf("  DXT match        : %-28s %s %dx%d  %zu bytes "
                        "byte-identical to rldds.py\n",
                        t.name.c_str(), format.c_str(), w, h, reference.size());
          } else {
            unmatched.push_back(t.name + " (" + e.path().filename().string() +
                                ")");
          }
          break;
        }
        if (!found) unmatched.push_back(t.name + " (no Python dump found)");
      }
      CHECK(matched == 13, "only %d of 13 textures are byte-identical to the "
            "Python decoder (%d compared)", matched, compared);
      for (const std::string& u : unmatched)
        std::printf("  DXT MISMATCH     : %s\n", u.c_str());
      std::printf("  DXT cross-check  : %d of %d decoded textures are "
                  "byte-identical to tools/arena/rldds.py\n",
                  matched, compared);
    }
  }

  std::printf("ARENA_RIP: %d PASS, %d FAIL\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
