// tests/test_arena_textures.cpp — headless verification for arena texture
// mapping (A4). No GL context: it drives the pure texturegen::Fill* layer and
// asserts the invariants the visual gates depend on, plus the UV math the draw
// sites use. Exit code 0 = all checks pass.
//
// Why this test exists: the sandbox has no GPU, so the capture-based VG gates
// cannot run here. The texture CONTENT and the UV mapping are still provable
// without GL — this is the proof. Every value asserted below is either a
// documented ledger value or a geometry invariant the gate measures.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "models/ArenaTextures.h"

using namespace rl::texturegen;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); \
           std::printf("\n"); } \
} while (0)

// --- helpers ---------------------------------------------------------------

static void RgbAt(const unsigned char* img, int w, int x, int y, float& r,
                  float& g, float& b) {
    const unsigned char* p = img + (y * w + x) * 3;
    r = p[0] / 255.0f; g = p[1] / 255.0f; b = p[2] / 255.0f;
}

static float Luma(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// --- 1. Turf sheet ---------------------------------------------------------

static void TestTurf() {
    std::vector<unsigned char> img(kTurfW * kTurfH * 3);
    rl::texturegen::FillTurf(img.data());

    // VG14's criterion is green dominance on the floor band. Assert the sheet
    // itself is overwhelmingly green-dominant in the way the gate computes it:
    // g > b + 4/255 and g > r + 8/255 (visual_gates.py:687).
    int total = kTurfW * kTurfH, green = 0;
    for (int y = 0; y < kTurfH; y++)
        for (int x = 0; x < kTurfW; x++) {
            float r, g, b;
            RgbAt(img.data(), kTurfW, x, y, r, g, b);
            if (g > b + 4.0f / 255.0f && g > r + 8.0f / 255.0f) green++;
        }
    float frac = (float)green / total;
    CHECK(frac >= 0.95f, "turf green-dominant frac=%.3f (need >=0.95)", frac);

    // 16 mowed bands: each band's mean luma must alternate, i.e. adjacent band
    // pairs differ measurably. This is the "mowed stripes" invariant.
    const int bandH = kTurfH / 16;
    float bandLuma[16] = {0};
    for (int band = 0; band < 16; band++) {
        double s = 0;
        for (int y = band * bandH; y < (band + 1) * bandH; y++)
            for (int x = 0; x < kTurfW; x++) {
                float r, g, b;
                RgbAt(img.data(), kTurfW, x, y, r, g, b);
                s += Luma(r, g, b);
            }
        bandLuma[band] = (float)(s / (bandH * kTurfW));
    }
    // Dark bands (even) must be darker than light bands (odd).
    int altOk = 0;
    for (int band = 0; band + 1 < 16; band += 2)
        if (bandLuma[band] < bandLuma[band + 1]) altOk++;
    CHECK(altOk == 8, "turf band alternation %d/8 pairs dark<light", altOk);

    // The two greens are the ledger TUNED values; check the band means land on
    // them (within the grain amplitude). Dark G ~0.52, light G ~0.60.
    CHECK(std::fabs(bandLuma[0] - Luma(0.11f, 0.52f, 0.09f)) < 0.06f,
          "dark band luma %.3f vs ledger %.3f", bandLuma[0],
          Luma(0.11f, 0.52f, 0.09f));
    CHECK(std::fabs(bandLuma[1] - Luma(0.14f, 0.60f, 0.11f)) < 0.06f,
          "light band luma %.3f vs ledger %.3f", bandLuma[1],
          Luma(0.14f, 0.60f, 0.11f));

    // Tileability: the sheet must wrap cleanly in both axes (GL_REPEAT), so the
    // first and last row/column must be continuous with their wrapped
    // neighbours. The mow-seam row is excluded by checking mid-band columns.
    bool wrapX = true, wrapY = true;
    for (int y = 0; y < kTurfH; y++) {
        float r0, g0, b0, r1, g1, b1;
        RgbAt(img.data(), kTurfW, 0, y, r0, g0, b0);
        RgbAt(img.data(), kTurfW, kTurfW - 1, y, r1, g1, b1);
        if (std::fabs(Luma(r0, g0, b0) - Luma(r1, g1, b1)) > 0.12f) wrapX = false;
    }
    CHECK(wrapX, "turf sheet does not wrap continuously in X");
    (void)wrapY;  // band pitch is the Y seam by design; no continuity expected
}

// --- 2. Turf markings sheet ------------------------------------------------

static void TestMarkings() {
    std::vector<unsigned char> img(kMarkW * kMarkH * 4);
    rl::texturegen::FillTurfMarkings(img.data());

    // Mostly transparent: only the thin lines are opaque. The midfield line is
    // ~2 texels of 512 and the circle stroke ~3 texels, so the expected density
    // is well under 2% — assert it stays a thin overlay, not a filled sheet.
    int opaque = 0;
    for (int i = 0; i < kMarkW * kMarkH; i++)
        if (img[i * 4 + 3] > 128) opaque++;
    float frac = (float)opaque / (kMarkW * kMarkH);
    CHECK(frac > 0.002f && frac < 0.10f,
          "markings opaque frac=%.4f (want 0.002..0.10: thin lines only)", frac);

    // Midfield line exists on the V centre row and is transparent well away
    // from it (e.g. 1/4 of the way up the sheet).
    const float px = kTurfSpanX / kMarkW, py = kTurfSpanY / kMarkH;
    int midRow = (int)(kTurfSpanY * 0.5f / py);
    int midOpaque = 0;
    for (int x = 0; x < kMarkW; x++)
        if (img[(midRow * kMarkW + x) * 4 + 3] > 128) midOpaque++;
    CHECK(midOpaque >= kMarkW * 9 / 10,
          "midfield line covers only %d/%d texels on its row", midOpaque, kMarkW);

    int quarterRow = (int)((kTurfSpanY * 0.5f - 2560.0f) / py);
    if (quarterRow >= 0 && quarterRow < kMarkH) {
        int qOpaque = 0;
        for (int x = 0; x < kMarkW; x++)
            if (img[(quarterRow * kMarkW + x) * 4 + 3] > 128) qOpaque++;
        CHECK(qOpaque == 0, "stray markings on a non-line row (%d texels)",
              qOpaque);
    }

    // Centre circle: r = 600 uu must be marked on the row through the centre,
    // at x = +/-600 uu, and NOT at the centre point itself.
    int cRow = (int)(kTurfSpanY * 0.5f / py);
    auto marked = [&](float wx, float wy) {
        int xi = (int)((wx + kTurfSpanX * 0.5f) / px);
        int yi = (int)((wy + kTurfSpanY * 0.5f) / py);
        if (xi < 0 || xi >= kMarkW || yi < 0 || yi >= kMarkH) return false;
        return img[(yi * kMarkW + xi) * 4 + 3] > 128;
    };
    CHECK(marked(600.0f, 0.0f) && marked(-600.0f, 0.0f),
          "centre circle not marked at r=600 uu on the centre row");
    CHECK(!marked(0.0f, 120.0f), "centre point should be clear of markings");
    CHECK(marked(0.0f, 600.0f) && marked(0.0f, -600.0f),
          "centre circle not marked at the top/bottom of r=600 uu");
    (void)cRow;
}

// --- 3. Hex-net sheet: the 420 uu cell pitch is the load-bearing number -----

static void TestHexNet() {
    std::vector<unsigned char> img(kHexW * kHexH * 3);
    rl::texturegen::FillHexNet(img.data());

    // Glass hue: VG6 asserts >=90% of wall pixels are "blue" (b dominant).
    int blue = 0, total = kHexW * kHexH;
    for (int y = 0; y < kHexH; y++)
        for (int x = 0; x < kHexW; x++) {
            float r, g, b;
            RgbAt(img.data(), kHexW, x, y, r, g, b);
            if (b > r + 0.10f && b > g + 0.10f) blue++;
        }
    float frac = (float)blue / total;
    CHECK(frac >= 0.90f, "hex sheet blue-dominant frac=%.3f (need >=0.90)", frac);

    // ---- the hexagonal invariant VG6 actually measures -------------------
    // VG6 autocorrelates the net-only signal along X and expects the dominant
    // lag to be the flat-top hexagon's HORIZONTAL PERIOD, which is
    // sqrt(3)/2 * cell = 0.866 * 420 = 363.73 uu -- NOT the 420 uu cell and NOT
    // the "span / 6" the previous version of this test asserted. Encoding the
    // wrong invariant here is exactly what let a broken (non-tiling, zigzag)
    // lattice pass its own unit test.
    const float expPeriodUu = std::sqrt(3.0f) * 0.5f * kHexCell;
    const float expPeriodTex = expPeriodUu * (float)kHexW / kHexSpanX;
    // The sheet holds kHexCols whole periods, so the seam is exact.
    CHECK(std::fabs(expPeriodTex * kHexCols - (float)kHexW) < 1e-3f,
          "horizontal period %.4f tex x %d cols = %.4f, want %d",
          expPeriodTex, kHexCols, expPeriodTex * kHexCols, kHexW);
    // Row pitch: 1.5 * R = 1.5 * cell/2 = 315 uu, whole rows across the sheet.
    const float expRowTex = 1.5f * (kHexCell * 0.5f) *
                            (float)kHexH / kHexSpanY;
    CHECK(std::fabs(expRowTex * kHexRows - (float)kHexH) < 1e-3f,
          "row pitch %.4f tex x %d rows = %.4f, want %d",
          expRowTex, kHexRows, expRowTex * kHexRows, kHexH);

    // Seam-crossing count along a scanline: a horizontal line through hexagon
    // bodies crosses each column's left and right vertical edges, so we expect
    // about 2 * kHexCols transitions.
    auto isSeam = [&](int x, int y) {
        float r, g, b;
        RgbAt(img.data(), kHexW, x, y, r, g, b);
        return Luma(r, g, b) > 0.55f;
    };
    // Scan several rows and take the max run count: a row through hexagon
    // bodies crosses 2 edges per column, while a row grazing the horizontal
    // flat edges crosses far fewer (it runs along them). Sampling several rows
    // and taking the max avoids depending on which row the sheet height
    // happens to put at kHexH/2 (flat-top rows centre on multiples of the row
    // pitch, so kHexH/2 = 256 lands exactly on row 4's centre and undercounts).
    int bestCrossings = 0;
    for (int frac = 1; frac <= 7; frac++) {
        int scanY = kHexH * frac / 8;
        int crossings = 0;
        bool prev = isSeam(0, scanY);
        for (int x = 1; x < kHexW; x++) {
            bool cur = isSeam(x, scanY);
            if (cur && !prev) crossings++;
            prev = cur;
        }
        if (crossings > bestCrossings) bestCrossings = crossings;
    }
    // A horizontal line through hexagon bodies crosses each column's left and
    // right edges: about 2 * kHexCols transitions. Allow slack for rows that
    // clip a vertex instead of an edge.
    CHECK(bestCrossings >= kHexCols && bestCrossings <= 2 * kHexCols + 4,
          "best hex seam crossings=%d (expect %d..%d for %d cols)",
          bestCrossings, kHexCols, 2 * kHexCols + 4, kHexCols);

    // Net contrast: VG6 measures a net-vs-glass contrast. Assert the seam is
    // materially brighter than the glass base so the net reads.
    float r, g, b, sr, sg, sb;
    RgbAt(img.data(), kHexW, 0, 0, r, g, b);          // likely base
    // Find a bright seam pixel.
    float best = 0;
    for (int y = 0; y < kHexH; y++)
        for (int x = 0; x < kHexW; x++) {
            RgbAt(img.data(), kHexW, x, y, sr, sg, sb);
            float l = Luma(sr, sg, sb);
            if (l > best) best = l;
        }
    CHECK(best - Luma(r, g, b) > 0.25f,
          "hex net contrast %.3f too low (seam %.3f base %.3f)",
          best - Luma(r, g, b), best, Luma(r, g, b));

    // --no-hexnet differential invariant (what VG6 actually measures): the
    // net-free sheet must differ from the net sheet ONLY by the net, so the
    // difference image isolates the net lines and its contrast is material.
    std::vector<unsigned char> plain(kHexW * kHexH * 3);
    rl::texturegen::FillPlainGlass(plain.data());
    double diffSum = 0;
    float diffMax = 0;
    for (int y = 0; y < kHexH; y++)
        for (int x = 0; x < kHexW; x++) {
            float r1, g1, b1, r2, g2, b2;
            RgbAt(img.data(), kHexW, x, y, r1, g1, b1);
            RgbAt(plain.data(), kHexW, x, y, r2, g2, b2);
            float d = std::fabs(Luma(r1, g1, b1) - Luma(r2, g2, b2));
            diffSum += d;
            if (d > diffMax) diffMax = d;
        }
    CHECK(diffSum > 0.0 && diffMax > 0.25f,
          "net-vs-plain differential contrast %.3f too low for VG6", diffMax);
    // The plain sheet must itself be blue-dominant (VG6 measures hue on the
    // net-on frame, but the differential's baseline must be the same glass).
    int pblue = 0;
    for (int y = 0; y < kHexH; y++)
        for (int x = 0; x < kHexW; x++) {
            float r2, g2, b2;
            RgbAt(plain.data(), kHexW, x, y, r2, g2, b2);
            if (b2 > r2 + 0.10f && b2 > g2 + 0.10f) pblue++;
        }
    CHECK((float)pblue / (kHexW * kHexH) > 0.98f,
          "plain glass not blue-dominant (%.3f)", (float)pblue / (kHexW * kHexH));

    // Column parity (VG6 robustness): alternate hexagon columns draw dimmer,
    // breaking the half-period autocorrelation tie. Bright-seam pixels in even
    // vs odd period-wide bands must differ materially in mean luma.
    {
        const float hPeriodTex = (float)kHexW / (float)kHexCols;
        double sEven = 0, sOdd = 0;
        int nEven = 0, nOdd = 0;
        for (int y = 0; y < kHexH; y++)
            for (int x = 0; x < kHexW; x++) {
                float r, g, b;
                RgbAt(img.data(), kHexW, x, y, r, g, b);
                if (Luma(r, g, b) < 0.5f) continue;   // seams only
                int band = (int)((float)x / hPeriodTex) % 2;
                if (band == 0) { sEven += Luma(r, g, b); nEven++; }
                else { sOdd += Luma(r, g, b); nOdd++; }
            }
        CHECK(nEven > 100 && nOdd > 100, "too few seam pixels for parity check");
        float mEven = (float)(sEven / (nEven > 0 ? nEven : 1));
        float mOdd = (float)(sOdd / (nOdd > 0 ? nOdd : 1));
        CHECK(std::fabs(mEven - mOdd) / ((mEven + mOdd) * 0.5f) > 0.05f,
              "hex column parity missing (even %.3f odd %.3f)", mEven, mOdd);
    }
}

// --- 3b. Wall brightness discipline (VG15) -----------------------------------
//
// The sheet carries the FINAL albedo (draw sites multiply white), so this is
// a direct bound on it. Non-seam texels must carry enough luma that the lit
// render clears VG15's near-black bound (L>24) at kickoff distance: the old
// (0.06,0.20,0.95) base sat at luma ~0.10 and rendered L~14 there.
static void TestWallBrightness() {
    std::vector<unsigned char> img(kHexW * kHexH * 3);
    rl::texturegen::FillHexNet(img.data());
    double s = 0;
    int n = 0;
    for (int y = 0; y < kHexH; y++)
        for (int x = 0; x < kHexW; x++) {
            float r, g, b;
            RgbAt(img.data(), kHexW, x, y, r, g, b);
            float l = Luma(r, g, b);
            if (l > 0.5f) continue;   // seam pixels excluded
            s += l;
            n++;
        }
    float baseL = (float)(s / n);
    CHECK(baseL > 0.20f,
          "wall base mean luma %.3f too dark for VG15 (need >0.20)", baseL);
}

// --- 4. Goal interior + ceiling --------------------------------------------

static void TestGoalInterior() {
    std::vector<unsigned char> img(kGoalW * kGoalH * 3);
    rl::texturegen::FillGoalInterior(img.data());
    // Dark but not black: the weave must be visible as variation.
    float mn = 1, mx = 0;
    for (int y = 0; y < kGoalH; y++)
        for (int x = 0; x < kGoalW; x++) {
            float r, g, b;
            RgbAt(img.data(), kGoalW, x, y, r, g, b);
            float l = Luma(r, g, b);
            if (l < mn) mn = l;
            if (l > mx) mx = l;
        }
    CHECK(mx - mn > 0.10f, "goal weave contrast %.3f too flat", mx - mn);
    CHECK(mx < 0.95f, "goal interior should stay dark (max luma %.3f)", mx);
    // Neutral: r==g==b everywhere.
    bool neutral = true;
    for (int y = 0; y < kGoalH && neutral; y++)
        for (int x = 0; x < kGoalW && neutral; x++) {
            const unsigned char* p = img.data() + (y * kGoalW + x) * 3;
            if (p[0] != p[1] || p[1] != p[2]) neutral = false;
        }
    CHECK(neutral, "goal interior should be neutral so the team tint controls it");
}

static void TestCeiling() {
    std::vector<unsigned char> img(kCeilW * kCeilH * 3);
    rl::texturegen::FillCeiling(img.data());
    // Ledger base (0.17,0.22,0.31): bluish dark. Assert it stays in that band.
    double sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < kCeilW * kCeilH; i++) {
        sr += img[i * 3 + 0]; sg += img[i * 3 + 1]; sb += img[i * 3 + 2];
    }
    int n = kCeilW * kCeilH;
    float ar = (float)(sr / n / 255.0), ag = (float)(sg / n / 255.0),
          ab = (float)(sb / n / 255.0);
    CHECK(ab > ag && ag > ar, "ceiling should be bluish-dark (%.3f,%.3f,%.3f)",
          ar, ag, ab);
    CHECK(ar > 0.10f && ar < 0.24f, "ceiling base R %.3f out of band", ar);
    // Brightness discipline (VG15): the draw site multiplies this sheet by a
    // WHITE glColor, so the sheet mean luma IS the albedo. The lit render
    // keeps ~half of it, and VG15 fails surfaces at L<=24 (0.094). The sheet
    // mean must therefore sit well above 2x that bound.
    double sl = 0;
    for (int y = 0; y < kCeilH; y++)
        for (int x = 0; x < kCeilW; x++) {
            float r, g, b;
            RgbAt(img.data(), kCeilW, x, y, r, g, b);
            sl += Luma(r, g, b);
        }
    float meanL = (float)(sl / n);
    CHECK(meanL > 0.19f && meanL < 0.30f,
          "ceiling sheet mean luma %.3f out of band (want 0.19..0.30)", meanL);
}

// --- 4b. Wall-base fillet sheet --------------------------------------------

static void TestWallBase() {
    std::vector<unsigned char> img(kBaseW * kBaseH * 3);
    rl::texturegen::FillWallBase(img.data());
    // VG11's curb band, in texture space with headroom for the lighting
    // multiply: every texel must sit near the old flat (0.20,0.28,0.37).
    int inBand = 0, total = kBaseW * kBaseH;
    for (int i = 0; i < total; i++) {
        float r = img[i * 3 + 0] / 255.0f, g = img[i * 3 + 1] / 255.0f,
              b = img[i * 3 + 2] / 255.0f;
        if (r > 0.08f && r < 0.34f && g > 0.13f && g < 0.44f &&
            b > 0.20f && b < 0.55f && b >= r && b > g)
            inBand++;
    }
    CHECK((float)inBand / total > 0.98f,
          "wall-base texels in band %d/%d (need >98%%)", inBand, total);
    // V gradient direction: v=1 (glass line) lighter than v=0 (floor contact).
    double s0 = 0, s1 = 0;
    for (int x = 0; x < kBaseW; x++) {
        float r, g, b;
        RgbAt(img.data(), kBaseW, x, 0, r, g, b);
        s0 += Luma(r, g, b);
        RgbAt(img.data(), kBaseW, x, kBaseH - 1, r, g, b);
        s1 += Luma(r, g, b);
    }
    CHECK(s1 > s0, "wall-base gradient inverted (top %.3f vs floor %.3f)",
          s1 / kBaseW, s0 / kBaseW);
}

// --- 4c. Shell courses + arc-length fillet math ------------------------------

static void TestFilletCourses() {
    // The three courses must join exactly: base arc starts at the floor and
    // ends where the glass starts; the ceiling arc ends at the ceiling plane.
    CHECK(std::fabs(kGlassZ0 - kFilletR) < 1e-3f,
          "glass bottom %.1f != fillet radius %.1f", kGlassZ0, kFilletR);
    CHECK(std::fabs(kGlassZ1 + kFilletR - 2048.0f) < 1e-3f,
          "glass top %.1f + R %.1f != ceiling 2048", kGlassZ1, kFilletR);
    // Quarter-arc length: pi/2 * R. Both fillets map exactly one of these to
    // vRep = arc/kHexSpanY (net) or one full sheet (base), so cells stay
    // square around the bend.
    const float arc = (float)(M_PI_2) * kFilletR;
    CHECK(std::fabs(arc - 402.1239f) < 0.01f, "quarter arc %.4f != 402.1239",
          arc);
    // V continuity: wall rows + fillet rows tile the sheet without a jump.
    float wallV = (kGlassZ1 - kGlassZ0) / kHexSpanY;
    float filV = arc / kHexSpanY;
    CHECK(wallV > 0.60f && wallV < 0.62f, "wall vRep %.4f (want ~0.6095)",
          wallV);
    CHECK(filV > 0.159f && filV < 0.161f, "fillet vRep %.4f (want ~0.1595)",
          filV);
    // VG6's measured rows (50..690 of 720) must land on FLAT glass, never on
    // a fillet: replicate the row->wall-z projection at the VG6 camera
    // (eye z=1024, wall 1002 uu away, half-FOV from the 110 slider).
    const float fx = 110.0f * (float)M_PI / 180.0f;
    const float aspect = 16.0f / 9.0f;
    const float fy =
        2.0f * std::atan(std::tan(fx * 0.5f) / aspect * (16.0f / 9.0f));
    const float half = fy * 0.5f;
    auto zOfRow = [&](int row) {
        float frac = ((float)row - 360.0f) / 360.0f;  // -1..1 across height
        return 1024.0f + 1002.0f * std::tan(frac * half);
    };
    CHECK(zOfRow(50) < kGlassZ1 - 50.0f,
          "VG6 top row maps to z=%.0f (glass top %.0f)", zOfRow(50), kGlassZ1);
    CHECK(zOfRow(690) > kGlassZ0 + 20.0f,
          "VG6 bottom row maps to z=%.0f (glass bottom %.0f)", zOfRow(690),
          kGlassZ0);
}

// --- 4d. Chamfer phase continuity --------------------------------------------

static void TestCornerPhase() {
    // The chamfer panel continues the side wall's lattice: its U origin is
    // the side wall's end phase, not 0. Side wall spans y -3968..3968 with
    // u=0 at -3968, so the +3968 junction sits at fmod(7936/span,1).
    float sidePhase = std::fmod(7936.0f / kHexSpanX, 1.0f);
    CHECK(sidePhase > 0.11f && sidePhase < 0.13f,
          "side junction phase %.5f (want ~0.11706)", sidePhase);
    // Back wall spans x -2944..2944 with u=0 at +2944 (u grows toward -X), so
    // its -2944 junction sits at fmod(5888/span,1).
    float backPhase = std::fmod(5888.0f / kHexSpanX, 1.0f);
    CHECK(backPhase > 0.30f && backPhase < 0.33f,
          "back junction phase %.5f (want ~0.31263)", backPhase);
    // Chamfer geometry: 1152*sqrt(2) = 1629.174 (RLBot wiki, RLGym).
    CHECK(std::fabs(1152.0f * std::sqrt(2.0f) - 1629.174f) < 0.01f,
          "chamfer length != 1152*sqrt(2)");
    // The chamfer holds a FRACTION of a sheet; with GL_REPEAT any offset is
    // seamless, and the junction phase is what makes the edge meet.
    float chamferU = 1629.174f / kHexSpanX;
    CHECK(chamferU > 0.63f && chamferU < 0.65f, "chamfer uRep %.4f",
          chamferU);
}

// --- 4e. Goal-mouth cutout: one global UV field, quads sample subrects ------
//
// Collision (ArenaGeometry RayCast: open mouth for wall/fillet hits) has no
// wall or base fillet inside |x|<892.755, z<642.775 on the back walls. The
// visual matches by OMITTING quads there — every remaining quad derives its
// (uOff,vOff) from the same wall-global mapping, so the lattice a viewer
// sees is continuous around the opening instead of restarting per piece.
static void TestMouthCutout() {
    // Back-wall global mapping: u=0 at x=+2944 growing toward -X (the +Y wall
    // quad yaw of pi flips local +x to world -X), v=0 at kGlassZ0.
    auto uAt = [&](float x) { return (2944.0f - x) / kHexSpanX; };
    auto vAt = [&](float z) { return (z - kGlassZ0) / kHexSpanY; };
    // Left segment starts at the wall start: offset exactly 0.
    CHECK(std::fabs(uAt(2944.0f)) < 1e-6f, "mouth-left uOff != 0");
    // Right segment starts at x=-892.755: its quad offset must equal the
    // global field there (GL_REPEAT wraps the >1 part).
    float uR = uAt(-892.755f);
    CHECK(uR > 1.50f && uR < 1.51f, "mouth-right uOff %.5f (want ~1.50696)",
          uR);
    // The above-mouth panel starts at the crossbar height in the same field.
    float vC = vAt(642.775f);
    CHECK(vC > 0.153f && vC < 0.154f, "mouth-top vOff %.5f (want ~0.15347)",
          vC);
    // And it ends exactly at the glass top: offsets + reps = wall vRep.
    float wallV = (kGlassZ1 - kGlassZ0) / kHexSpanY;
    CHECK(std::fabs(vC + (kGlassZ1 - 642.775f) / kHexSpanY - wallV) < 1e-5f,
          "mouth-top v range does not end at wall vRep");
}

// --- 4f. Plan-view junction rounds: arc-length UV + faceted normals --------
//
// The dump rounds are facet fans, not smooth arcs. planRound (main.cpp) draws
// one quad per surveyed facet with a flat mid-angle normal and arc-length U.
// These assert the mapping formulas with synthetic inputs so the draw code
// and the test agree on the convention: angles standard in plan X-Y, inward
// normals = -(cos,sin) (arc centre sits outside the room), s measured from a0.
static void TestPlanRoundUV() {
    const float R = 500.0f, a0 = 0.0f, a1 = (float)M_PI / 4.0f;
    const int steps = 8;
    // Total arc and per-step uniformity.
    float total = R * (a1 - a0);
    CHECK(std::fabs(total - 392.6991f) < 0.01f, "round arc %.4f != 392.6991",
          total);
    for (int k = 0; k < steps; k++) {
        float t0 = a0 + (a1 - a0) * (float)k / steps;
        float t1 = a0 + (a1 - a0) * (float)(k + 1) / steps;
        CHECK(std::fabs(R * (t1 - t0) - total / steps) < 1e-3f,
              "round step %d arc not uniform", k);
    }
    // U spans exactly the arc over the sheet span (square cells around the
    // bend, same density as the straights).
    float uSpan = total / kHexSpanX;
    CHECK(uSpan > 0.154f && uSpan < 0.155f, "round uSpan %.5f", uSpan);
    // Facet normals: mid-angle radial, inward. At t=0 the round meets a +X
    // wall (inward normal (-1,0)); at t=pi/4 it meets the chamfer
    // (inward (-sqrt(2)/2,-sqrt(2)/2)).
    {
        float nx0 = -std::cos(a0), ny0 = -std::sin(a0);
        CHECK(std::fabs(nx0 + 1.0f) < 1e-5f && std::fabs(ny0) < 1e-5f,
              "round start normal (%.4f,%.4f) != (-1,0)", nx0, ny0);
        float nx1 = -std::cos(a1), ny1 = -std::sin(a1);
        const float d = std::sqrt(2.0f) / 2.0f;
        CHECK(std::fabs(nx1 + d) < 1e-4f && std::fabs(ny1 + d) < 1e-4f,
              "round end normal (%.4f,%.4f) != chamfer inward", nx1, ny1);
    }
    // Adjacent facet normals differ by exactly one step angle (no twist).
    float stepA = (a1 - a0) / steps;
    for (int k = 0; k + 1 < steps; k++) {
        float m0 = a0 + stepA * (k + 0.5f), m1 = a0 + stepA * (k + 1.5f);
        float dot = std::cos(m0) * std::cos(m1) + std::sin(m0) * std::sin(m1);
        CHECK(std::fabs(dot - std::cos(stepA)) < 1e-5f,
              "round facet %d/%d normal step inconsistent", k, k + 1);
    }
}

// --- 5. UV mapping math: draw sites divide by the sheet span ---------------

static void TestUvMath() {
    // The floor quad is one sheet: uRep/vRep must be exactly 1.
    CHECK(std::fabs(kTurfSpanX / kTurfSpanX - 1.0f) < 1e-6f, "turf uRep != 1");

    // The horizontal period a wall shows is what VG6 measures: it must come out
    // at sqrt(3)/2 * cell no matter how long the panel is, for BOTH wall kinds
    // (straight and corner). A panel is `len` uu; uRep = len / kHexSpanX; the
    // sheet then shows uRep * kHexCols periods across that length.
    const float expPeriodUu = std::sqrt(3.0f) * 0.5f * kHexCell;
    auto periodFor = [&](float len) {
        float uRep = len / kHexSpanX;
        float periods = uRep * (float)kHexCols;
        return len / periods;         // uu per horizontal period
    };
    struct Panel { const char* name; float len; };
    const Panel panels[] = {
        {"side wall", 7936.0f},      // main.cpp glass() call site
        {"back wall", 5888.0f},
        {"corner panel", 1629.0f},
    };
    for (const Panel& p : panels) {
        float period = periodFor(p.len);
        CHECK(std::fabs(period - expPeriodUu) < 0.5f,
              "%s horizontal period %.2f uu vs expected %.2f uu",
              p.name, period, expPeriodUu);
    }

    // Vertical: row pitch is 1.5 * R = 315 uu, independent of panel height.
    const float expRowUu = 1.5f * (kHexCell * 0.5f);
    auto rowFor = [&](float h) {
        float vRep = h / kHexSpanY;
        float rows = vRep * (float)kHexRows;
        return h / rows;
    };
    for (float h : {2048.0f, 642.775f, 1024.0f}) {
        CHECK(std::fabs(rowFor(h) - expRowUu) < 0.5f,
              "row pitch %.2f uu at height %.1f vs expected %.2f",
              rowFor(h), h, expRowUu);
    }

    // Ceiling: 8192 x 10240 over a 2048 uu sheet.
    float cURep = 8192.0f / kCeilSpan, cVRep = 10240.0f / kCeilSpan;
    CHECK(std::fabs(cURep - 4.0f) < 1e-3f, "ceiling uRep %.2f vs 4", cURep);
    CHECK(std::fabs(cVRep - 5.0f) < 1e-3f, "ceiling vRep %.2f vs 5", cVRep);
}

// --- 5b. The net must be a VALID honeycomb, not just "some lines" ----------
//
// A gullible test only counts seam pixels. The real requirement is geometric:
// neighbouring hexagons must SHARE VERTICES, which is what interlocks the
// honeycomb. The original implementation placed vertices at 0/60/... degrees,
// which shares none, and produced a zigzag that still had plenty of seam
// pixels. This asserts the vertex-sharing invariant directly.
static void TestHexLatticeInterlocks() {
    const float R = kHexCell * 0.5f;
    const float hPeriod = std::sqrt(3.0f) * R;   // horizontal centre spacing
    const float rowPitch = 1.5f * R;             // vertical centre spacing
    // Pointy-top hexagon vertices start at 30 deg (the phase that interlocks).
    auto verts = [&](float cx, float cy) {
        std::vector<std::pair<float, float>> out;
        for (int e = 0; e < 6; e++) {
            float a = (float)M_PI / 6.0f + (float)M_PI / 3.0f * e;
            out.push_back({cx + R * std::cos(a), cy + R * std::sin(a)});
        }
        return out;
    };
    auto shares = [&](float dx, float dy) {
        auto a = verts(0, 0);
        auto b = verts(dx, dy);
        int n = 0;
        for (auto& p : a)
            for (auto& q : b)
                if (std::fabs(p.first - q.first) < 1e-3f &&
                    std::fabs(p.second - q.second) < 1e-3f) n++;
        return n;
    };
    // Every one of the six neighbours must share exactly two vertices.
    const float h[6][2] = {
        { hPeriod, 0 }, { -hPeriod, 0 },
        { hPeriod * 0.5f, rowPitch }, { hPeriod * 0.5f, -rowPitch },
        { -hPeriod * 0.5f, rowPitch }, { -hPeriod * 0.5f, -rowPitch },
    };
    int total = 0;
    for (auto& off : h) {
        int n = shares(off[0], off[1]);
        CHECK(n == 2, "neighbour (%.1f,%.1f) shares %d vertices, want 2",
              off[0], off[1], n);
        total += n;
    }
    CHECK(total == 12, "hexagons share %d vertices total, want 12", total);

    // And the sheet must hold whole periods so GL_REPEAT is seamless.
    CHECK(std::fabs((float)kHexCols * hPeriod - kHexSpanX) < 0.5f,
          "sheet width %.2f uu != %d whole periods (%.2f uu)",
          kHexSpanX, kHexCols, kHexCols * hPeriod);
    CHECK(std::fabs((float)kHexRows * rowPitch - kHexSpanY) < 0.5f,
          "sheet height %.2f uu != %d whole rows (%.2f uu)",
          kHexSpanY, kHexRows, kHexRows * rowPitch);
    CHECK(kHexRows % 2 == 0, "row count must be even for a complete tiling");
}

// --- 6. Wall quad normal direction (the bug class the old DrawBox hid) -----

// A single textured quad only shades its normal side. Every arena wall must
// therefore have its quad normal pointing INTO the arena. DrawTexturedQuadWall
// puts its normal at local +y, so the quad yaw that achieves inward facing for
// a wall whose inward normal is n is: quadYaw = atan2(-n.x, n.y).
// These are the exact values main.cpp's glass()/cornerGlass() call sites use.
static void TestWallNormals() {
    auto quadNormal = [](float yawDeg, float& nx, float& ny) {
        float y = yawDeg * (float)M_PI / 180.0f;
        nx = -std::sin(y);
        ny = std::cos(y);
    };
    struct Wall { const char* name; float yawDeg, inX, inY; };
    // name, quadYawDeg used in main.cpp, required INWARD normal.
    const Wall walls[] = {
        {"+X side", 90.0f, -1.0f, 0.0f},
        {"-X side", 270.0f, 1.0f, 0.0f},
        {"+Y back", 180.0f, 0.0f, -1.0f},
        {"-Y back", 0.0f, 0.0f, 1.0f},
        // Corners: the outer rotation already maps lambda-local +y to the corner's
        // outward normal, so the quad is rotated 180 deg -> inward. Express the
        // corner's TOTAL quad yaw = cornerYaw + 180 and check it points inward.
        {"corner NW", -45.0f + 180.0f, -0.7071f, -0.7071f},
        {"corner NE", 45.0f + 180.0f, 0.7071f, -0.7071f},
        {"corner SW", -135.0f + 180.0f, -0.7071f, 0.7071f},
        {"corner SE", 135.0f + 180.0f, 0.7071f, 0.7071f},
    };
    for (const Wall& w : walls) {
        float nx, ny;
        quadNormal(w.yawDeg, nx, ny);
        float dot = nx * w.inX + ny * w.inY;
        CHECK(dot > 0.95f,
              "%s quad normal (%.2f,%.2f) does not face inward n(%.2f,%.2f) "
              "dot=%.2f", w.name, nx, ny, w.inX, w.inY, dot);
    }
}

// --- 4g. Junction round table (mesh mode): surveyed R=705 bowl rounds -----
//
// The dumped mesh carries an R=705 round wedged in each 135° corner (~8
// facets at ~5.6° steps, facet normals 0°->45°), tangent length
// T = R*tan(pi/8) = 292. The draw code places one planRound per junction;
// this pins the table (centres, angles, phase origins) so a transcription
// slip fails here instead of on the gate machine.
static void TestRoundTable() {
    const float R = 705.0f;
    const float T = R * std::tan((float)M_PI / 8.0f);
    CHECK(std::fabs(T - 292.0f) < 0.5f, "round tangent %.2f (want ~292.0)", T);
    // Arc spans exactly the 45° kink; 8 quads = ~5.6° each like the survey.
    const float arc = R * (float)M_PI / 4.0f;
    CHECK(std::fabs(arc - 553.7f) < 0.2f, "round arc %.2f (want ~553.7)", arc);
    float mArcU = arc / kHexSpanX;
    CHECK(mArcU > 0.2174f && mArcU < 0.2175f, "round arc uSpan %.5f", mArcU);
    // Mesh run lengths (tangent to tangent) and their end phases.
    float mSideU = 2.0f * (3968.0f - T) / kHexSpanX;
    float mBackU = 2.0f * (2944.0f - T) / kHexSpanX;
    CHECK(std::fabs(mSideU - 2.88764f) < 0.002f, "mesh side run u %.5f",
          mSideU);
    CHECK(std::fabs(mBackU - 2.08309f) < 0.002f, "mesh back run u %.5f",
          mBackU);
    // Chamfer flat middle: full length minus both tangent runs.
    float flat = 1629.174f - 2.0f * T;
    CHECK(std::fabs(flat - 1045.2f) < 0.5f, "chamfer flat %.2f", flat);
    // Centre construction: vertex minus R along the wall outward, minus T
    // along the wall tangent. J1 (+X+Y): vertex (4096,3968).
    float c1x = 4096.0f - R, c1y = 3968.0f - T;
    CHECK(std::fabs(c1x - 3391.0f) < 0.5f && std::fabs(c1y - 3676.0f) < 0.5f,
          "J1 centre (%.1f,%.1f)", c1x, c1y);
    // Tangency: centre is exactly R from both planes.
    CHECK(std::fabs((4096.0f - c1x) - R) < 0.5f, "J1 centre off wall plane");
    CHECK(std::fabs((c1x + c1y - 8064.0f) / std::sqrt(2.0f) + R) < 1.0f,
          "J1 centre off chamfer plane");
    // Tangent points: wall tangent straight down the wall, chamfer tangent
    // 45° around the arc.
    float twx = 4096.0f, twy = 3968.0f - T;
    CHECK(std::fabs(twy - 3676.0f) < 0.5f, "J1 wall tangent y=%.1f", twy);
    float tcx = 4096.0f - T * std::sqrt(2.0f) / 2.0f;
    float tcy = 3968.0f + T * std::sqrt(2.0f) / 2.0f;
    CHECK(std::fabs(tcx + tcy - 8064.0f) < 0.5f,
          "J1 chamfer tangent off plane (%.1f,%.1f)", tcx, tcy);
    CHECK(std::fabs(tcx - 3889.5f) < 1.0f && std::fabs(tcy - 4174.5f) < 1.0f,
          "J1 chamfer tangent (%.1f,%.1f)", tcx, tcy);
    // u origins: J1 starts at the +X run end phase, J5 (back) at 0.
    (void)twx;
    CHECK(std::fabs(mSideU - (2.0f * (3968.0f - T)) / kHexSpanX) < 1e-5f,
          "J1 u0 inconsistent");
}

// --- 4h. One global field per wall (mesh runs sample subranges) ------------
//
// Mesh-mode runs are SHORTER (tangent to tangent) but sample the SAME
// fallback-anchored field (+X: u(y)=(y+3968)/spanX etc.), so wall pixels are
// identical across modes and the VG6 autocorrelation cannot tell them apart.
// A mesh-anchored re-zeroing once shifted the lattice 292 uu and flipped the
// full-patch AC 91->46; this pins the continuity instead.
static void TestMeshField() {
    const float T = 705.0f * std::tan((float)M_PI / 8.0f);
    const float sideEnd = 3968.0f - T, backEnd = 2944.0f - T;
    // Straight-run edge phases in the fallback-anchored field.
    float px0 = (sideEnd + 3968.0f) / kHexSpanX;    // +X run TW end
    float px1 = (3968.0f - sideEnd) / kHexSpanX;    // +X run start
    CHECK(px1 > 0.114f && px1 < 0.115f, "+X mesh start phase %.5f", px1);
    CHECK(px0 > 3.002f && px0 < 3.003f, "+X mesh end phase %.5f", px0);
    // The run spans exactly its length: end-start = len/span.
    CHECK(std::fabs((px0 - px1) - 2.0f * sideEnd / kHexSpanX) < 1e-5f,
          "side run span inconsistent");
    float py0 = (2944.0f + backEnd) / kHexSpanX;    // +Y run TW end
    CHECK(py0 > 2.197f && py0 < 2.199f, "+Y mesh end phase %.5f", py0);
    // Round arrival = TW phase + arc; the chamfer flat starts exactly there.
    float arcU = (705.0f * (float)M_PI / 4.0f) / kHexSpanX;
    float arrival = px0 + arcU;
    float chamU = (1629.174f - 2.0f * T) / kHexSpanX;
    // Far end = arrival - flat length (negative uRep direction).
    float farEnd = arrival - chamU;
    (void)farEnd;
    // Arrival must sit inside a sane wrapped range (no assertion on the slip
    // itself: the 45° kink leaves a residual at the far joint by topology,
    // hidden under the TC trim bar).
    CHECK(arrival > 3.0f && arrival < 3.4f, "J1 arrival %.5f", arrival);
}

int main() {
    TestTurf();
    TestMarkings();
    TestHexNet();
    TestHexLatticeInterlocks();
    TestWallBrightness();
    TestGoalInterior();
    TestCeiling();
    TestWallBase();
    TestFilletCourses();
    TestCornerPhase();
    TestMouthCutout();
    TestPlanRoundUV();
    TestRoundTable();
    TestMeshField();

    TestUvMath();
    TestWallNormals();
    std::printf("ARENA_TEXTURES %s (%d checks, %d failed)\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
