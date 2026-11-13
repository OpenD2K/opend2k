#include "formats/ps1_models.hpp"

#include "formats/ps1_disc.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>

namespace d2k::formats::ps1 {

namespace {

constexpr float kScale = 1.0f / 8.0f;  // 256 PS1 units per tile -> 32 px

struct Exe {
    std::span<const std::uint8_t> d;
    std::uint32_t loadAddr = 0;
    bool inRange(std::uint32_t off, std::size_t n) const { return off <= d.size() && n <= d.size() - off; }
    std::uint32_t u32(std::uint32_t off) const {
        if (!inRange(off, 4)) return 0;
        return std::uint32_t(d[off]) | std::uint32_t(d[off + 1]) << 8 | std::uint32_t(d[off + 2]) << 16 |
               std::uint32_t(d[off + 3]) << 24;
    }
    std::int16_t s16(std::uint32_t off) const {
        if (!inRange(off, 2)) return 0;
        return std::int16_t(std::uint16_t(d[off] | d[off + 1] << 8));
    }
    // RAM address -> file offset (0 = outside the image).
    std::uint32_t fileOf(std::uint32_t addr) const {
        if (addr < loadAddr) return 0;
        const std::uint32_t o = addr - loadAddr + 0x800;
        return o < d.size() ? o : 0;
    }
};

// Words per polygon record by the type's low byte (see the header comment).
int recordWords(std::uint32_t type) {
    switch (type & 0xff) {
    case 0x0c: return 4;   // G3 lit, untextured: colour + 3 (normal, vertex)
    case 0x14: return 5;   // G4 lit, untextured
    case 0x0d: return 6;   // GT3 lit: 3 uv + 3 pairs
    case 0x15: return 7;   // GT4 lit: 4 uv (first pair in the upper halves of uv2/uv3) + 3 pairs
    case 0x0e: return 6;   // G3 coloured: 3 colours + 3 pairs
    case 0x16: return 8;   // G4 coloured
    case 0x0f: return 9;   // GT3 coloured: 3 colours + 3 uv + 3 pairs
    case 0x17: return 11;  // GT4 coloured
    default: return 0;
    }
}

struct Poly {
    int nv = 3;
    bool tex = false, coloured = false, noLight = false;
    std::uint32_t col[4] = {0x808080, 0x808080, 0x808080, 0x808080};
    int u[4] = {}, v[4] = {};
    int norm[4] = {}, vert[4] = {};
};

struct Hmd {
    std::vector<std::vector<Poly>> blocks;  // index = block
    std::vector<std::int32_t> coordT;       // 3 per coordinate
    std::vector<int> coordParent;
    std::uint32_t vertOff = 0, normOff = 0;  // file offsets
    int maxVert = -1, maxNorm = -1;
};

bool parseHmd(const Exe& x, std::uint32_t fo, Hmd& h, std::string& err) {
    auto W = [&](std::uint32_t k) { return x.u32(fo + 4 * k); };
    if (W(0) != 0x50) { err = "not an HMD"; return false; }
    const std::uint32_t phs = W(2), nb = W(3);
    if (nb == 0 || nb > 64 || phs < 4 + nb) { err = "bad HMD header"; return false; }
    const std::uint32_t nHdr = W(phs + 1);
    if (nHdr < 4 || nHdr > 16) { err = "bad primitive header"; return false; }
    const std::uint32_t prim = W(phs + 2) & 0x7fffffff, vs = W(phs + 3) & 0x7fffffff, ns = W(phs + 4) & 0x7fffffff,
                        cs = W(phs + 5) & 0x7fffffff;
    h.vertOff = fo + 4 * vs;
    h.normOff = fo + 4 * ns;
    const std::uint32_t nCoord = W(cs);
    if (nCoord > 64) { err = "bad coordinate section"; return false; }
    for (std::uint32_t c = 0; c < nCoord; ++c) {
        const std::uint32_t k = cs + 1 + 20 * c;
        for (int j = 0; j < 3; ++j) h.coordT.push_back(std::int32_t(W(k + 6 + j)));
        const std::uint32_t sup = W(k + 19);
        h.coordParent.push_back(sup > cs ? int((sup - (cs + 1)) / 20) : -1);
    }
    h.blocks.resize(nb);
    for (std::uint32_t bi = 0; bi < nb; ++bi) {
        const std::uint32_t b = W(4 + bi);
        if (!b) continue;
        const std::uint32_t nt = W(b + 2) & 0x7fffffff;
        if (nt > 16) { err = "bad block"; return false; }
        for (std::uint32_t t = 0; t < nt; ++t) {
            const std::uint32_t type = W(b + 3 + 3 * t), n = (W(b + 4 + 3 * t) >> 16) & 0x7fff, p = prim + W(b + 5 + 3 * t);
            const int sz = recordWords(type);
            if (!sz) { err = "unknown polygon type"; return false; }
            const int lo = int(type & 0xff);
            for (std::uint32_t r = 0; r < n; ++r) {
                const std::uint32_t o = fo + 4 * (p + r * std::uint32_t(sz));
                if (!x.inRange(o, std::size_t(sz) * 4)) { err = "polygon outside the file"; return false; }
                std::uint32_t w[11];
                for (int j = 0; j < sz; ++j) w[j] = x.u32(o + 4 * std::uint32_t(j));
                Poly pg;
                pg.nv = (lo & 0x10) ? 4 : 3;
                pg.tex = lo & 1;
                pg.coloured = lo & 2;
                pg.noLight = type & 0x00200000;
                int q = 0;
                if (pg.coloured) {
                    for (int j = 0; j < pg.nv; ++j) pg.col[j] = w[j] & 0xffffff;
                    q = pg.nv;
                } else if (!pg.tex) {
                    for (int j = 0; j < pg.nv; ++j) pg.col[j] = w[0] & 0xffffff;
                    q = 1;
                }
                int pairs = 0;
                if (pg.tex) {
                    for (int j = 0; j < pg.nv; ++j) {
                        pg.u[j] = int(w[q + j] & 0xff);
                        pg.v[j] = int((w[q + j] >> 8) & 0xff);
                    }
                    if (pg.nv == 4) {
                        pg.norm[0] = int(w[q + 2] >> 16);
                        pg.vert[0] = int(w[q + 3] >> 16);
                        pairs = 1;
                    }
                    q += pg.nv;
                }
                for (; pairs < pg.nv && q < sz; ++pairs, ++q) {
                    pg.norm[pairs] = int(w[q] & 0xffff);
                    pg.vert[pairs] = int(w[q] >> 16);
                }
                if (pairs != pg.nv) { err = "short polygon record"; return false; }
                for (int j = 0; j < pg.nv; ++j) {
                    h.maxVert = std::max(h.maxVert, pg.vert[j]);
                    h.maxNorm = std::max(h.maxNorm, pg.norm[j]);
                }
                h.blocks[bi].push_back(pg);
            }
        }
    }
    if (h.maxVert < 0 || h.maxVert > 4000 || h.maxNorm > 4000) { err = "bad vertex indices"; return false; }
    if (!x.inRange(h.vertOff, std::size_t(h.maxVert + 1) * 8) || !x.inRange(h.normOff, std::size_t(h.maxNorm + 1) * 8)) {
        err = "vertices outside the file";
        return false;
    }
    return true;
}

// Translation of coordinate c in model space: its chain of parents WITHOUT the root coordinate
// (the root holds a constant centring offset).
void chainOffset(const Hmd& h, int c, float out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int guard = 0; c > 0 && c < int(h.coordParent.size()) && guard < 64; ++guard) {
        for (int j = 0; j < 3; ++j) out[j] += float(h.coordT[std::size_t(c) * 3 + std::size_t(j)]);
        c = h.coordParent[std::size_t(c)];
    }
}

bool decodeTim(const Exe& x, std::uint32_t o, Model3D& m) {
    if (x.u32(o) != 0x10 || (x.u32(o + 4) & 0xF) != 8) return false;  // 4bpp with CLUT
    const std::uint32_t clutLen = x.u32(o + 8);
    const int cw = x.s16(o + 16), ch = x.s16(o + 18);
    if (cw < 16 || ch < 1 || clutLen != std::uint32_t(cw * ch * 2 + 12)) return false;
    std::uint32_t clut[16];
    for (int i = 0; i < 16; ++i) {
        const std::uint16_t c = std::uint16_t(x.s16(o + 20 + std::uint32_t(i) * 2));
        const std::uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
        clut[i] = r | g << 8 | b << 16 | (c == 0 ? 0u : 0xFFu << 24);  // 0x0000 = transparent (PS1 rule)
    }
    const std::uint32_t p = o + 8 + clutLen;
    const std::uint32_t imgLen = x.u32(p);
    const int iw = x.s16(p + 8), ih = x.s16(p + 10);
    if (iw <= 0 || ih <= 0 || imgLen != std::uint32_t(iw * ih * 2 + 12) || !x.inRange(p + 12, std::size_t(iw) * ih * 2))
        return false;
    m.texW = iw * 4;
    m.texH = ih;
    m.texture.resize(std::size_t(m.texW) * std::size_t(m.texH));
    for (int y = 0; y < ih; ++y)
        for (int bx = 0; bx < iw * 2; ++bx) {
            const std::uint8_t b = x.d[p + 12 + std::uint32_t(y * iw * 2 + bx)];
            m.texture[std::size_t(y * m.texW + bx * 2)] = clut[b & 15];
            m.texture[std::size_t(y * m.texW + bx * 2 + 1)] = clut[b >> 4];
        }
    return true;
}

bool isBuildingName(std::string_view n) {
    static constexpr std::string_view kNames[] = {"wtrap", "cnyrd",    "silo",     "refin",    "lfac",     "repair",
                                                  "a_barrak", "h_barrak", "o_barrak", "gntur", "rcktur",   "outpst",
                                                  "tech",     "hfac",     "starpt",   "a_palace", "h_palace", "o_palace",
                                                  "i_palace", "ix",       "wall"};
    return std::find(std::begin(kNames), std::end(kNames), n) != std::end(kNames);
}

// PS1 model space -> our world axes. The PS1 world is x east, y down, z NORTH (a right-handed
// frame: the GTE camera looks from the south, so z grows into the screen); ours is x east, y up,
// z south, so the map is (x, -y, -z): a rotation, not a mirror (an earlier reading kept z and
// mirrored every model north-south, which showed the open backs to the default camera).
// Buildings: `zShift` (whole tiles, PS1 units) moves the model's front ground row onto the art
// box's bottom edge; the renderer adds the art box bottom (Anchor::ArtBottomLeft).
struct Xform {
    float zShift = 0;
    void pos(const float in[3], float out[3]) const {
        out[0] = in[0] * kScale;
        out[1] = -in[1] * kScale;
        out[2] = -(in[2] - zShift) * kScale;
    }
};

// Vertex j of a polygon in PS1 model space (with the block's coordinate offset).
void psVertex(const Exe& x, const Hmd& h, const Poly& p, int j, const float off[3], float out[3]) {
    const std::uint32_t vo = h.vertOff + std::uint32_t(p.vert[j]) * 8;
    out[0] = float(x.s16(vo)) + off[0];
    out[1] = float(x.s16(vo + 2)) + off[1];
    out[2] = float(x.s16(vo + 4)) + off[2];
}

// Appends the triangles of a block to a part, in our world axes. Triangles are emitted
// counter-clockwise seen from outside (the PS1 winding is clockwise: its stored normals point
// against the right-hand cross product of every record).
void addBlock(const Exe& x, const Hmd& h, const Model3D& m, int bi, const Xform& xf, MeshPart& part, float house) {
    if (bi < 1 || bi >= int(h.blocks.size())) return;
    float off[3];
    chainOffset(h, bi - 1, off);
    auto vtx = [&](const Poly& p, int j) {
        MeshVertex v;
        float ps[3];
        psVertex(x, h, p, j, off, ps);
        xf.pos(ps, v.pos);
        const std::uint32_t no = h.normOff + std::uint32_t(p.norm[j]) * 8;
        v.nrm[0] = float(x.s16(no)) / 4096.0f;
        v.nrm[1] = -float(x.s16(no + 2)) / 4096.0f;
        v.nrm[2] = -float(x.s16(no + 4)) / 4096.0f;
        if (p.tex && m.texW > 0) {
            v.uv[0] = (float(p.u[j]) + 0.5f) / float(m.texW);
            v.uv[1] = (float(p.v[j]) + 0.5f) / float(m.texH);
        }
        v.textured = p.tex ? 1.0f : 0.0f;
        // Textured: 0x80 = texel x 1 (GPU modulation); untextured: the colour itself.
        const float k = p.tex ? 1.0f / 128.0f : 1.0f / 255.0f;
        const std::uint32_t c = p.col[j];
        if (p.coloured || !p.tex) {
            v.rgba[0] = float(c & 0xff) * k;
            v.rgba[1] = float((c >> 8) & 0xff) * k;
            v.rgba[2] = float((c >> 16) & 0xff) * k;
        }
        v.lit = p.coloured ? 0.0f : 1.0f;
        v.house = house;
        return v;
    };
    for (const Poly& p : h.blocks[std::size_t(bi)]) {
        static constexpr int kTri[2][3] = {{0, 2, 1}, {1, 2, 3}};
        for (int t = 0; t < (p.nv == 4 ? 2 : 1); ++t)
            for (int j : kTri[t]) part.tris.push_back(vtx(p, j));
    }
}

// Mean colour of a polygon as drawn (texels under its uv triangle x vertex colour), 0..1.
void polyColour(const Model3D& m, const Poly& p, float out[3]) {
    float vc[3] = {0, 0, 0};
    for (int j = 0; j < p.nv; ++j)
        for (int c = 0; c < 3; ++c) vc[c] += float((p.col[j] >> (8 * c)) & 0xff) / float(p.nv);
    const float k = p.tex ? 1.0f / 128.0f : 1.0f / 255.0f;
    for (int c = 0; c < 3; ++c) out[c] = (p.coloured || !p.tex) ? vc[c] * k : 1.0f;
    if (!p.tex || m.texW <= 0) return;
    float sum[3] = {0, 0, 0};
    int n = 0;
    constexpr int kSteps = 8;
    for (int t = 0; t < (p.nv == 4 ? 2 : 1); ++t) {
        const int a = t ? 1 : 0, b = t ? 3 : 1, c = 2;
        for (int i = 0; i <= kSteps; ++i)
            for (int j = 0; i + j <= kSteps; ++j) {
                const float wa = float(i) / kSteps, wb = float(j) / kSteps, wc = 1 - wa - wb;
                const int u = std::clamp(int(wa * float(p.u[a]) + wb * float(p.u[b]) + wc * float(p.u[c])), 0, m.texW - 1);
                const int v = std::clamp(int(wa * float(p.v[a]) + wb * float(p.v[b]) + wc * float(p.v[c])), 0, m.texH - 1);
                const std::uint32_t px = m.texture[std::size_t(v * m.texW + u)];
                if (!(px >> 24)) continue;
                for (int ch = 0; ch < 3; ++ch) sum[ch] += float((px >> (8 * ch)) & 0xff) / 255.0f;
                ++n;
            }
    }
    for (int ch = 0; ch < 3; ++ch) out[ch] *= n ? sum[ch] / float(n) : 0.5f;
}

// The PS1 camera never turns, so building bodies only have the faces it can see: the north
// (back) side and the space under roofs are open, and a turned camera looks into a hollow shell.
// Closes them with "curtains": every boundary edge (an edge of only one polygon) above the ground
// whose polygon lies on its south side drops a vertical quad to the ground, facing north (away
// from the PS1 camera). The quads are one-sided (MeshVertex::oneSided), so the view the models
// were made for is unchanged; seen from behind they are walls in the polygon's mean colour.
void addCurtains(const Exe& x, const Hmd& h, const Model3D& m, const Xform& xf, MeshPart& part) {
    if (h.blocks.size() < 2) return;
    const float off[3] = {0, 0, 0};  // block 1 = coordinate 0 (the root, ignored)
    const auto& polys = h.blocks[1];
    static constexpr int kPerim[2][4][2] = {{{0, 1}, {1, 2}, {2, 0}, {0, 0}}, {{0, 1}, {1, 3}, {3, 2}, {2, 0}}};
    using Key = std::array<int, 6>;
    auto key = [](const float a[3], const float b[3]) {
        Key k{int(std::lround(a[0])), int(std::lround(a[1])), int(std::lround(a[2])),
              int(std::lround(b[0])), int(std::lround(b[1])), int(std::lround(b[2]))};
        if (std::lexicographical_compare(k.begin() + 3, k.end(), k.begin(), k.begin() + 3))
            std::rotate(k.begin(), k.begin() + 3, k.end());
        return k;
    };
    std::map<Key, int> uses;
    std::vector<std::array<float, 3>> pv;
    auto load = [&](const Poly& p) {
        pv.assign(std::size_t(p.nv), {});
        for (int j = 0; j < p.nv; ++j) psVertex(x, h, p, j, off, pv[std::size_t(j)].data());
    };
    for (const Poly& p : polys) {
        load(p);
        for (int e = 0; e < (p.nv == 4 ? 4 : 3); ++e) ++uses[key(pv[std::size_t(kPerim[p.nv == 4][e][0])].data(), pv[std::size_t(kPerim[p.nv == 4][e][1])].data())];
    }
    for (const Poly& p : polys) {
        load(p);
        float c[3] = {0, 0, 0};
        for (const auto& v : pv)
            for (int i = 0; i < 3; ++i) c[i] += v[std::size_t(i)] / float(p.nv);
        float col[3];
        bool haveCol = false;
        for (int e = 0; e < (p.nv == 4 ? 4 : 3); ++e) {
            const float* A = pv[std::size_t(kPerim[p.nv == 4][e][0])].data();
            const float* B = pv[std::size_t(kPerim[p.nv == 4][e][1])].data();
            if (uses[key(A, B)] != 1) continue;
            if (std::max(-A[1], -B[1]) < 8) continue;  // on the ground already
            const float ex = B[0] - A[0], ez = B[2] - A[2], le = std::sqrt(ex * ex + ez * ez);
            if (le < 1) continue;
            float nx = ez / le, nz = -ex / le;  // horizontal, perpendicular to the edge
            const float mx = (A[0] + B[0]) / 2 - c[0], my = (A[1] + B[1]) / 2 - c[1], mz = (A[2] + B[2]) / 2 - c[2];
            const float s = mx * nx + mz * nz;
            // A vertical polygon (its edge straight above its centre) is its own wall.
            if (std::fabs(s) < 0.2f * std::sqrt(mx * mx + my * my + mz * mz)) continue;
            if (s < 0) nx = -nx, nz = -nz;  // away from the polygon
            if (nz < 0.25f) continue;       // only sides facing PS1 north (away from its camera)
            if (!haveCol) {
                polyColour(m, p, col);
                haveCol = true;
            }
            const float A0[3] = {A[0], 0, A[2]}, B0[3] = {B[0], 0, B[2]};
            const float* q[4] = {A, B, B0, A0};
            MeshVertex v[4];
            for (int i = 0; i < 4; ++i) {
                xf.pos(q[i], v[i].pos);
                v[i].nrm[0] = nx;
                v[i].nrm[1] = 0;
                v[i].nrm[2] = -nz;
                v[i].textured = 0;
                for (int ch = 0; ch < 3; ++ch) v[i].rgba[ch] = col[ch] * 0.9f;
                v[i].lit = 1;
                v[i].oneSided = 1;
            }
            // Counter-clockwise seen from outside (the normal's side).
            const float ux = v[1].pos[0] - v[0].pos[0], uy = v[1].pos[1] - v[0].pos[1], uz = v[1].pos[2] - v[0].pos[2];
            const float wx = v[2].pos[0] - v[0].pos[0], wy = v[2].pos[1] - v[0].pos[1], wz = v[2].pos[2] - v[0].pos[2];
            const float cx = uy * wz - uz * wy, cz = ux * wy - uy * wx, cy = uz * wx - ux * wz;
            const bool ccw = cx * v[0].nrm[0] + cy * v[0].nrm[1] + cz * v[0].nrm[2] > 0;
            static constexpr int kQuad[2][6] = {{0, 1, 2, 0, 2, 3}, {0, 2, 1, 0, 3, 2}};
            for (int i : kQuad[ccw ? 0 : 1]) part.tris.push_back(v[i]);
        }
    }
}

}  // namespace

bool parseModels(std::span<const std::uint8_t> data, std::vector<Model3D>& out, std::string& err,
                 std::vector<ModelInfo>* info) {
    out.clear();
    if (info) info->clear();
    Exe x{data};
    if (data.size() < 0x1000 || std::memcmp(data.data(), "PS-X EXE", 8) != 0) {
        err = "not a PS-X EXE";
        return false;
    }
    x.loadAddr = x.u32(0x18);
    // The table: find the record named "trike" ({hmd, tim - 8, name[12]}).
    static constexpr char kFirst[] = "trike\0\0\0";
    std::uint32_t table = 0;
    for (std::uint32_t o = 8; o + 20 <= data.size(); o += 4)
        if (std::memcmp(&data[o], kFirst, 8) == 0 && x.fileOf(x.u32(o - 8)) && x.fileOf(x.u32(o - 4))) {
            table = o - 8;
            break;
        }
    if (!table) {
        err = "model table not found in DUNE.EXE";
        return false;
    }
    for (std::uint32_t r = table; r + 20 <= data.size(); r += 20) {
        const std::uint32_t hf = x.fileOf(x.u32(r)), tf = x.fileOf(x.u32(r + 4));
        char nm[13] = {};
        std::memcpy(nm, &data[r + 8], 12);
        if (!hf || !tf || !nm[0]) break;
        bool printable = true;
        for (char c : std::string_view(nm))
            if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) printable = false;
        if (!printable) break;
        Model3D m;
        m.name = nm;
        const bool building = isBuildingName(m.name);
        m.anchor = building ? Model3D::Anchor::ArtBottomLeft : Model3D::Anchor::Centre;
        m.pointSample = true;
        Hmd h;
        std::string e;
        if (!parseHmd(x, hf, h, e)) {
            err = m.name + ": " + e;
            return false;
        }
        if (!decodeTim(x, tf + 8, m)) {
            err = m.name + ": no texture";
            return false;
        }
        const int nb = int(h.blocks.size());
        Xform xf;
        if (building && nb >= 2) {
            // The front (south) row of the ground polygons onto the art box's bottom edge, in whole
            // tiles: most 3x3 models start one tile north of their origin (z 256), pads and
            // palaces at 0.
            float zmin = 1e9f;
            const float none[3] = {0, 0, 0};
            for (const Poly& p : h.blocks[1])
                for (int j = 0; j < p.nv; ++j) {
                    float v[3];
                    psVertex(x, h, p, j, none, v);
                    if (v[1] > -24) zmin = std::min(zmin, v[2]);
                }
            if (zmin < 1e8f) xf.zShift = 256.0f * std::max(0.0f, std::round(zmin / 256.0f));
        }
        if (nb >= 10) {
            addBlock(x, h, m, 1, xf, m.body, 0);
            addBlock(x, h, m, 2, xf, m.house, 2);
            addBlock(x, h, m, 8, xf, m.shadow, 0);
            if (nb >= 11) {
                addBlock(x, h, m, 9, xf, m.turret, 0);
                float p[3];
                chainOffset(h, 8, p);
                xf.pos(p, m.turretPivot);
                // A marker above the body's top sits on the turret (combat tank).
                float bodyTop = -1e9f, houseLow = 1e9f;
                for (const auto& v : m.body.tris) bodyTop = std::max(bodyTop, v.pos[1]);
                for (const auto& v : m.house.tris) houseLow = std::min(houseLow, v.pos[1]);
                m.houseOnTurret = !m.house.empty() && houseLow > bodyTop;
            }
        } else {
            // Other layouts (4-block flyers / missile, ix, wall): lit blocks = body, no-light = shadow.
            for (int bi = 1; bi < nb; ++bi) {
                if (h.blocks[std::size_t(bi)].empty()) continue;
                const bool shadow = h.blocks[std::size_t(bi)].front().noLight;
                addBlock(x, h, m, bi, xf, shadow ? m.shadow : m.body, 0);
            }
        }
        // The PS1 shadow polygons fall north-east (its light comes from the south-west); ours
        // (terrain, sprites) fall south-east: mirror them north-south about the body's ground
        // footprint so they agree with the rest of the battlefield.
        {
            float z0 = 1e9f, z1 = -1e9f;
            for (const auto& v : m.body.tris)
                if (v.pos[1] < 3) z0 = std::min(z0, v.pos[2]), z1 = std::max(z1, v.pos[2]);
            if (z0 <= z1)
                for (auto& v : m.shadow.tris) v.pos[2] = z0 + z1 - v.pos[2];
        }
        if (building && nb >= 10) addCurtains(x, h, m, xf, m.body);
        if (info) {
            ModelInfo mi;
            mi.name = m.name;
            mi.blocks = nb;
            mi.coords = int(h.coordParent.size());
            for (const auto& b : h.blocks) mi.polygons += int(b.size());
            mi.vertices = h.maxVert + 1;
            mi.building = building;
            info->push_back(mi);
        }
        out.push_back(std::move(m));
    }
    if (out.empty()) {
        err = "no models in the table";
        return false;
    }
    return true;
}

bool loadModels(Disc& disc, std::vector<Model3D>& out, std::string& err, std::vector<ModelInfo>* info) {
    Disc::DirEntry e;
    if (!disc.find("DUNE.EXE", e)) {
        err = "DUNE.EXE not found on the disc";
        return false;
    }
    const std::vector<std::uint8_t> exe = disc.readForm1(e.lba, e.size);
    if (exe.size() != e.size) {
        err = "cannot read DUNE.EXE";
        return false;
    }
    return parseModels(exe, out, err, info);
}

std::string modelForUnit(std::string_view g) {
    struct Map { std::string_view group, model; };
    static constexpr Map kMap[] = {
        {"TRIKE", "trike"},         {"RAIDER", "trike"},         {"STEALTH_RAIDER", "trike"}, {"QUAD", "quad"},
        {"HARVESTER", "harvst"},    {"COMBAT_TANK", "cmbtnk"},   {"MCV", "mcv"},              {"MISSILE_TANK", "msltnk"},
        {"DEVIATOR", "dvator"},     {"SIEGE_TANK", "sgetnk"},    {"SONIC_TANK", "sontnk"},    {"DEVASTATOR", "devast"},
        {"CARRYALL", "carry"},      {"ORNITHOPTER", "thop"},     {"DEATH_HAND", "dthand"},    {"FRIGATE", "frigat"},
    };
    for (const auto& m : kMap)
        if (m.group == g) return std::string(m.model);
    return {};
}

std::string modelForBuilding(std::string_view name, std::string_view g) {
    if (g == "BARRACKS") {
        if (name.starts_with("HARKONNEN")) return "h_barrak";
        if (name.starts_with("ORDOS")) return "o_barrak";
        if (name.starts_with("ATREIDES")) return "a_barrak";
        return {};  // Fremen barracks: no PS1 model
    }
    struct Map { std::string_view group, model; };
    static constexpr Map kMap[] = {
        {"CONSTRUCTION_YARD", "cnyrd"},    {"WIND_TRAP", "wtrap"},          {"REFINERY", "refin"},
        {"MEDIUM_GUN_TURRET", "gntur"},    {"LARGE_GUN_TURRET", "rcktur"},  {"OUTPOST", "outpst"},
        {"SP_OUTPOST", "outpst"},          {"HIGH_TECH_FACTORY", "tech"},   {"AT_HIGH_TECH_FACTORY", "tech"},
        {"LIGHT_FACTORY", "lfac"},         {"SILO", "silo"},                {"HEAVY_FACTORY", "hfac"},
        {"STAR_PORT", "starpt"},           {"REPAIR_PAD", "repair"},        {"RESEARCH_CENTRE", "ix"},
        {"ATREIDES_PALACE", "a_palace"},   {"HARKONNEN_PALACE", "h_palace"}, {"ORDOS_PALACE", "o_palace"},
        {"IMPERIAL_PALACE", "i_palace"},
    };
    for (const auto& m : kMap)
        if (m.group == g) return std::string(m.model);
    return {};
}

}  // namespace d2k::formats::ps1
