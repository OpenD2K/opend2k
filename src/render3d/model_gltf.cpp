#define CGLTF_IMPLEMENTATION
#include "render3d/model_gltf.hpp"

#include "render3d/model_anim.hpp"

#include <SDL3/SDL.h>
#include <cgltf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace d2k::render3d {

namespace {

constexpr float kTile = 32.0f;

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// glTF colours (baseColorFactor, COLOR_0) are linear; the renderer works in display (sRGB) values
// like the sprites and tiles.
float linearToSrgb(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f;
}

bool isUnderTurret(const cgltf_node* n) {
    for (; n; n = n->parent)
        if (n->name && iequals(n->name, "turret")) return true;
    return false;
}

// The embedded (or data-URI-free buffer view) PNG of a texture -> RGBA8.
bool decodePng(const cgltf_image* img, int& w, int& h, std::vector<std::uint32_t>& px) {
    if (!img || !img->buffer_view || !img->buffer_view->buffer || !img->buffer_view->buffer->data) return false;
    if (img->mime_type && std::strcmp(img->mime_type, "image/png") != 0) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
    SDL_IOStream* io = SDL_IOFromConstMem(bytes, img->buffer_view->size);
    if (!io) return false;
    SDL_Surface* s = SDL_LoadPNG_IO(io, true);
    if (!s) return false;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);  // bytes R, G, B, A
    SDL_DestroySurface(s);
    if (!c) return false;
    w = c->w;
    h = c->h;
    px.resize(std::size_t(c->w) * std::size_t(c->h));
    for (int y = 0; y < c->h; ++y)
        std::memcpy(&px[std::size_t(y) * std::size_t(c->w)], static_cast<const std::uint8_t*>(c->pixels) + y * c->pitch,
                    std::size_t(c->w) * 4);
    SDL_DestroySurface(c);
    return true;
}

bool decodeImage(const cgltf_image* img, formats::Model3D& m) { return decodePng(img, m.texW, m.texH, m.texture); }

// glTF emissive (emissiveTexture x emissiveFactor x KHR_materials_emissive_strength) of the textured
// material, folded into the base colour texture's alpha (docs/3d-models.md "Emission"): opaque texels
// keep alpha >= 128, an emissive one gets 255 - 127 x its emission (the brightest channel, capped at 1);
// model_fs decodes it when the draw's emission strength (FU map.w) is above 0. Cut-out texels (alpha
// < 128) are left alone. Returns whether any texel glows.
bool foldEmission(const cgltf_material& mat, formats::Model3D& m) {
    if (m.texture.empty() || !mat.emissive_texture.texture) return false;
    float k = std::max({mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2]});
    if (mat.has_emissive_strength) k *= mat.emissive_strength.emissive_strength;
    if (k <= 0.0f) return false;
    int ew = 0, eh = 0;
    std::vector<std::uint32_t> e;
    if (!decodePng(mat.emissive_texture.texture->image, ew, eh, e) || ew <= 0 || eh <= 0) return false;
    bool any = false;
    for (int y = 0; y < m.texH; ++y) {
        const int ey = std::min(eh - 1, int((std::int64_t(y) * eh) / m.texH));
        for (int x = 0; x < m.texW; ++x) {
            std::uint32_t& c = m.texture[std::size_t(y) * std::size_t(m.texW) + std::size_t(x)];
            const std::uint32_t a = c >> 24;
            if (a < 128) continue;
            const int ex = std::min(ew - 1, int((std::int64_t(x) * ew) / m.texW));
            const std::uint32_t q = e[std::size_t(ey) * std::size_t(ew) + std::size_t(ex)];
            const float v = std::min(1.0f, float(std::max({q & 0xFF, (q >> 8) & 0xFF, (q >> 16) & 0xFF})) / 255.0f * k);
            const std::uint32_t na = 255u - std::uint32_t(std::lround(v * 127.0f));
            if (na < 255u) any = true;
            c = (c & 0x00FFFFFFu) | (std::min(a, na) << 24);
        }
    }
    return any;
}

// A node's local TRS (glTF defaults; a matrix is decomposed, no shear assumed).
void nodeTrs(const cgltf_node& n, float t[3], float r[4], float sc[3]) {
    t[0] = t[1] = t[2] = 0;
    r[0] = r[1] = r[2] = 0;
    r[3] = 1;
    sc[0] = sc[1] = sc[2] = 1;
    if (n.has_matrix) {
        const float* m = n.matrix;
        t[0] = m[12], t[1] = m[13], t[2] = m[14];
        float c[3][3];
        for (int k = 0; k < 3; ++k) {
            sc[k] = std::sqrt(m[k * 4] * m[k * 4] + m[k * 4 + 1] * m[k * 4 + 1] + m[k * 4 + 2] * m[k * 4 + 2]);
            for (int j = 0; j < 3; ++j) c[k][j] = sc[k] > 1e-12f ? m[k * 4 + j] / sc[k] : (j == k ? 1.0f : 0.0f);
        }
        // rotation matrix (columns c[k]) -> quaternion; R(row j, col k) = c[k][j]
        const float tr = c[0][0] + c[1][1] + c[2][2];
        if (tr > 0) {
            const float q = std::sqrt(tr + 1) * 2;
            r[3] = 0.25f * q;
            r[0] = (c[1][2] - c[2][1]) / q;
            r[1] = (c[2][0] - c[0][2]) / q;
            r[2] = (c[0][1] - c[1][0]) / q;
        } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
            const float q = std::sqrt(1 + c[0][0] - c[1][1] - c[2][2]) * 2;
            r[3] = (c[1][2] - c[2][1]) / q;
            r[0] = 0.25f * q;
            r[1] = (c[1][0] + c[0][1]) / q;
            r[2] = (c[2][0] + c[0][2]) / q;
        } else if (c[1][1] > c[2][2]) {
            const float q = std::sqrt(1 + c[1][1] - c[0][0] - c[2][2]) * 2;
            r[3] = (c[2][0] - c[0][2]) / q;
            r[0] = (c[1][0] + c[0][1]) / q;
            r[1] = 0.25f * q;
            r[2] = (c[2][1] + c[1][2]) / q;
        } else {
            const float q = std::sqrt(1 + c[2][2] - c[0][0] - c[1][1]) * 2;
            r[3] = (c[0][1] - c[1][0]) / q;
            r[0] = (c[2][0] + c[0][2]) / q;
            r[1] = (c[2][1] + c[1][2]) / q;
            r[2] = 0.25f * q;
        }
        return;
    }
    if (n.has_translation) std::memcpy(t, n.translation, 3 * sizeof(float));
    if (n.has_rotation) std::memcpy(r, n.rotation, 4 * sizeof(float));
    if (n.has_scale) std::memcpy(sc, n.scale, 3 * sizeof(float));
}

// Up to 4 (joint, weight) pairs -> bytes; weights normalised to sum 255 (the heaviest absorbs the
// rounding). Joints beyond 255 cannot be stored: the vertex stays rigid (all weights 0).
void packWeights(const unsigned j[4], const float w[4], formats::MeshVertex& v) {
    float sum = 0;
    for (int k = 0; k < 4; ++k) sum += std::max(0.0f, w[k]);
    std::memset(v.joint, 0, sizeof v.joint);
    std::memset(v.weight, 0, sizeof v.weight);
    if (sum <= 1e-8f) return;
    int total = 0, heavy = 0;
    for (int k = 0; k < 4; ++k) {
        if (j[k] > 255) {
            std::memset(v.weight, 0, sizeof v.weight);
            return;
        }
        v.joint[k] = std::uint8_t(j[k]);
        const int q = int(std::lround(std::max(0.0f, w[k]) / sum * 255.0f));
        v.weight[k] = std::uint8_t(std::clamp(q, 0, 255));
        total += v.weight[k];
        if (v.weight[k] > v.weight[heavy]) heavy = k;
    }
    v.weight[heavy] = std::uint8_t(std::clamp(int(v.weight[heavy]) + 255 - total, 0, 255));
}

void readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
}

// Minimal JSON walking on cgltf's jsmn tokens (frame maps, pose specs).
struct Json {
    std::string text;
    std::vector<jsmntok_t> tok;
    bool parse(const std::string& path) {
        readFile(path, text);
        if (text.empty()) return false;
        jsmn_parser p;
        jsmn_init(&p);
        const int n = jsmn_parse(&p, text.data(), text.size(), nullptr, 0);
        if (n <= 0) return false;
        tok.resize(std::size_t(n));
        jsmn_init(&p);
        return jsmn_parse(&p, text.data(), text.size(), tok.data(), tok.size()) == n;
    }
    // Index of the token after the subtree at i.
    int skip(int i) const {
        int end = i + 1;
        for (int k = 0; k < tok[std::size_t(i)].size; ++k) end = skip(end);
        return end;
    }
    std::string str(int i) const {
        const auto& t = tok[std::size_t(i)];
        return text.substr(std::size_t(t.start), std::size_t(t.end - t.start));
    }
    float num(int i) const { return std::strtof(str(i).c_str(), nullptr); }
    bool is(int i, const char* s) const { return tok[std::size_t(i)].type == JSMN_STRING && str(i) == s; }
    // The value token of key `k` in the object at i, -1 none.
    int get(int obj, const char* k) const {
        if (obj < 0 || tok[std::size_t(obj)].type != JSMN_OBJECT) return -1;
        int c = obj + 1;
        for (int m = 0; m < tok[std::size_t(obj)].size; ++m) {
            if (is(c, k)) return c + 1;
            c = skip(c + 1);
        }
        return -1;
    }
};

}  // namespace

bool loadGltfModel(const std::string& path, const GltfAdjust& adj, formats::Model3D& out, std::string& err) {
    out = formats::Model3D{};
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) {
        err = "cannot parse " + path;
        return false;
    }
    if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        err = "cannot load the buffers of " + path;
        return false;
    }
    const cgltf_material* texMat = nullptr;
    for (cgltf_size i = 0; i < data->materials_count && !texMat && !adj.geometryOnly; ++i) {
        const auto& pbr = data->materials[i].pbr_metallic_roughness;
        if (data->materials[i].has_pbr_metallic_roughness && pbr.base_color_texture.texture &&
            decodeImage(pbr.base_color_texture.texture->image, out))
            texMat = &data->materials[i];
    }
    if (texMat) out.emissiveInAlpha = foldEmission(*texMat, out);
    const float yaw = adj.yawDeg * 3.14159265f / 180.0f, cy = std::cos(yaw), sy = std::sin(yaw);
    auto place = [&](const float p[3], float o[3], bool dir) {
        const float x = p[0] * adj.scale, z = p[2] * adj.scale;
        o[0] = (x * cy - z * sy) * (dir ? 1 : kTile);
        o[2] = (x * sy + z * cy) * (dir ? 1 : kTile);
        o[1] = dir ? p[1] : (p[1] * adj.scale + adj.y) * kTile;
    };
    // Anchor points: nodes named fx_* (any case), then the models.ini entries (same name replaces).
    auto addAnchor = [&](const std::string& name, const float p[3], float size, bool onTurret) {
        formats::Model3D::AnchorPoint a;
        a.name = name;
        place(p, a.pos, false);
        a.size = size;
        a.onTurret = onTurret;
        for (auto& o : out.anchors)
            if (o.name == name) {
                o = a;
                return;
            }
        out.anchors.push_back(a);
    };
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->name || std::strlen(node->name) < 3 || !iequals(std::string_view(node->name, 3), "fx_")) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        const float p[3] = {world[12], world[13], world[14]};
        const float size = std::sqrt(world[0] * world[0] + world[1] * world[1] + world[2] * world[2]);
        std::string name(node->name);
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        addAnchor(name, p, size > 1e-4f ? size : 1.0f, isUnderTurret(node));
    }
    for (const auto& a : adj.anchors) addAnchor(a.name, a.pos, a.size, false);
    std::sort(out.anchors.begin(), out.anchors.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    // Skeleton (docs/3d-models.md "Animation"): every node (palette index = node index), the
    // place matrix (glTF tiles -> model px, as `place` above), the animations.
    auto& sk = out.skeleton;
    const bool rig = data->nodes_count > 0 && data->nodes_count <= 255;
    if (rig) {
        sk.nodes.resize(data->nodes_count);
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            const cgltf_node& n = data->nodes[ni];
            auto& o = sk.nodes[ni];
            o.parent = n.parent ? int(n.parent - data->nodes) : -1;
            nodeTrs(n, o.t, o.r, o.s);
        }
        const float sc = adj.scale * kTile;
        float* m = sk.place;
        std::fill(m, m + 16, 0.0f);
        m[0] = cy * sc, m[2] = sy * sc;  // x' = (x cy - z sy) s, z' = (x sy + z cy) s
        m[8] = -sy * sc, m[10] = cy * sc;
        m[5] = sc;
        m[13] = adj.y * kTile;
        m[15] = 1;
        for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
            const cgltf_animation& a = data->animations[ai];
            formats::Model3D::Skeleton::Animation an;
            an.name = a.name ? a.name : ("anim" + std::to_string(ai));
            for (cgltf_size ci = 0; ci < a.channels_count; ++ci) {
                const cgltf_animation_channel& c = a.channels[ci];
                if (!c.target_node || !c.sampler || !c.sampler->input || !c.sampler->output) continue;
                formats::Model3D::Skeleton::Channel ch;
                ch.node = int(c.target_node - data->nodes);
                if (c.target_path == cgltf_animation_path_type_translation) ch.path = 0;
                else if (c.target_path == cgltf_animation_path_type_rotation) ch.path = 1;
                else if (c.target_path == cgltf_animation_path_type_scale) ch.path = 2;
                else continue;  // morph weights: not supported
                ch.step = c.sampler->interpolation == cgltf_interpolation_type_step;
                const bool cubic = c.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
                const int nc = ch.path == 1 ? 4 : 3;
                const cgltf_size keys = c.sampler->input->count;
                ch.times.resize(keys);
                ch.values.resize(keys * std::size_t(nc));
                for (cgltf_size k = 0; k < keys; ++k) {
                    cgltf_accessor_read_float(c.sampler->input, k, &ch.times[k], 1);
                    // cubic spline: (in-tangent, value, out-tangent) per key; the values only (linear)
                    cgltf_accessor_read_float(c.sampler->output, cubic ? k * 3 + 1 : k, &ch.values[k * std::size_t(nc)],
                                              cgltf_size(nc));
                }
                if (keys) an.duration = std::max(an.duration, ch.times.back());
                an.channels.push_back(std::move(ch));
            }
            sk.animations.push_back(std::move(an));
        }
    }
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float world[16], normalM[16];
        cgltf_node_transform_world(node, world);
        std::memcpy(normalM, world, sizeof world);  // uniform scales only: the upper 3x3 is fine
        // Skinned (glTF: the mesh node's own transform is ignored): per joint, its rest world
        // matrix x inverse bind matrix; the vertices are stored in that bind pose.
        const cgltf_skin* skin = rig ? node->skin : nullptr;
        std::vector<float> jointBind;  // 16 per skin joint
        if (skin) {
            jointBind.resize(skin->joints_count * 16);
            for (cgltf_size j = 0; j < skin->joints_count; ++j) {
                float jw[16], ibm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                cgltf_node_transform_world(skin->joints[j], jw);
                if (skin->inverse_bind_matrices) cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16);
                mat4Mul(jw, ibm, &jointBind[j * 16]);
            }
            sk.skinned = true;
            // The palette (render3d/model_anim.cpp) assumes the file's rest pose is the bind pose:
            // then every joint's rest world x inverse bind matrix is the same (the mesh's bind matrix).
            float worst = 0;
            for (cgltf_size j = 1; j < skin->joints_count; ++j)
                for (int e = 0; e < 16; ++e) worst = std::max(worst, std::abs(jointBind[j * 16 + unsigned(e)] - jointBind[unsigned(e)]));
            if (worst > 1e-3f)
                SDL_Log("3D models: %s: skin rest pose differs from its bind pose (%.4f); poses may be off", path.c_str(),
                        double(worst));
        }
        const bool turret = isUnderTurret(node);
        formats::MeshPart& part = turret ? out.turret : out.body;
        if (turret && node->name && iequals(node->name, "turret")) {
            const float p[3] = {world[12], world[13], world[14]};
            place(p, out.turretPivot, false);
        }
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *col = nullptr, *house = nullptr,
                                 *joints = nullptr, *weights = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_joints && at.index == 0) joints = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) weights = at.data;
                else if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_color && at.index == 0) col = at.data;
                else if (at.name && iequals(at.name, "_HOUSE")) house = at.data;
            }
            if (!pos) continue;
            float factor[4] = {1, 1, 1, 1};
            const bool textured = (prim.material == texMat && texMat && uv) || (adj.geometryOnly && uv && prim.material &&
                                     prim.material->pbr_metallic_roughness.base_color_texture.texture);
            float gloss = 0.02f, metal = 0, matHouse = 0, lit = 1;
            if (const cgltf_material* m = prim.material) {
                if (m->has_pbr_metallic_roughness) {
                    std::memcpy(factor, m->pbr_metallic_roughness.base_color_factor, sizeof factor);
                    // > 0 also selects the glTF lighting in model_fs (fill light)
                    gloss = std::clamp(1.0f - m->pbr_metallic_roughness.roughness_factor, 0.02f, 1.0f);
                    metal = std::clamp(m->pbr_metallic_roughness.metallic_factor, 0.0f, 1.0f);
                }
                // A material named HOUSE* is house-coloured (tools/models convention).
                if (m->name && std::string_view(m->name).substr(0, 5) == "HOUSE") matHouse = 1;
                // Emissive: drawn unshaded in the base colour. Real light sources (a material named
                // GLOW / LIGHT / LAMP / FLASH, any case, e.g. "fx_lamp") keep glowing at night (lit 0);
                // other emissive parts (flat-shaded skin, cloth on the infantry) follow the time of
                // day's light (lit 0.35: model_fs tints them like the billboards).
                if (std::max({m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]}) > 0.05f) {
                    std::string n = m->name ? m->name : "";
                    for (char& ch : n) ch = char(std::toupper(static_cast<unsigned char>(ch)));
                    const bool glows = n.find("GLOW") != std::string::npos || n.find("LIGHT") != std::string::npos ||
                                       n.find("LAMP") != std::string::npos || n.find("FLASH") != std::string::npos;
                    lit = glows ? 0.0f : 0.35f;
                }
            }
            const cgltf_size n = prim.indices ? prim.indices->count : pos->count;
            const std::size_t first = part.tris.size();
            for (cgltf_size k = 0; k < n; ++k) {
                const cgltf_size vi = prim.indices ? cgltf_accessor_read_index(prim.indices, k) : k;
                formats::MeshVertex v;
                float p[3] = {}, w[3];
                cgltf_accessor_read_float(pos, vi, p, 3);
                const float* xf = world;  // this vertex's rest transform
                float skinM[16];
                if (skin && joints && weights) {
                    unsigned jj[4] = {};
                    float ww[4] = {};
                    cgltf_accessor_read_uint(joints, vi, jj, 4);
                    cgltf_accessor_read_float(weights, vi, ww, 4);
                    std::fill(skinM, skinM + 16, 0.0f);
                    unsigned node4[4] = {};
                    for (int k = 0; k < 4; ++k) {
                        if (jj[k] >= skin->joints_count) ww[k] = 0;
                        if (ww[k] <= 0) continue;
                        for (int e = 0; e < 16; ++e) skinM[e] += ww[k] * jointBind[jj[k] * 16 + unsigned(e)];
                        node4[k] = unsigned(skin->joints[jj[k]] - data->nodes);
                    }
                    const float wsum = ww[0] + ww[1] + ww[2] + ww[3];
                    if (wsum > 1e-6f) {
                        for (float& e : skinM) e /= wsum;
                        xf = skinM;
                        packWeights(node4, ww, v);
                    }
                } else if (rig) {
                    const unsigned self[4] = {unsigned(ni), 0, 0, 0};
                    const float one[4] = {1, 0, 0, 0};
                    packWeights(self, one, v);
                }
                for (int r = 0; r < 3; ++r) w[r] = xf[r] * p[0] + xf[4 + r] * p[1] + xf[8 + r] * p[2] + xf[12 + r];
                place(w, v.pos, false);
                if (nrm) {
                    float q[3] = {}, d[3];
                    cgltf_accessor_read_float(nrm, vi, q, 3);
                    const float* nm = xf == world ? normalM : xf;
                    for (int r = 0; r < 3; ++r) d[r] = nm[r] * q[0] + nm[4 + r] * q[1] + nm[8 + r] * q[2];
                    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    if (l > 1e-8f)
                        for (float& c : d) c /= l;
                    place(d, v.nrm, true);
                }
                if (uv) cgltf_accessor_read_float(uv, vi, v.uv, 2);
                float c[4] = {1, 1, 1, 1};
                if (col) cgltf_accessor_read_float(col, vi, c, cgltf_num_components(col->type));
                for (int r = 0; r < 3; ++r) v.rgba[r] = linearToSrgb(c[r] * factor[r]);
                v.rgba[3] = c[3] * factor[3];
                v.house = matHouse;
                if (house) cgltf_accessor_read_float(house, vi, &v.house, 1);
                v.gloss = gloss;
                v.metal = metal;
                v.lit = lit;
                v.textured = textured ? 1.0f : 0.0f;
                part.tris.push_back(v);
            }
            if (!nrm)  // flat normals
                for (std::size_t t = first; t + 2 < part.tris.size(); t += 3) {
                    float* a = part.tris[t].pos;
                    float* b = part.tris[t + 1].pos;
                    float* c = part.tris[t + 2].pos;
                    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    float nn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                    if (l > 1e-8f)
                        for (float& x : nn) x /= l;
                    for (int j = 0; j < 3; ++j) std::memcpy(part.tris[t + std::size_t(j)].nrm, nn, sizeof nn);
                }
        }
    }
    cgltf_free(data);
    if (out.empty()) {
        err = "no triangles in " + path;
        return false;
    }
    out.anchor = formats::Model3D::Anchor::Centre;
    return true;
}

bool loadGltfNodeNames(const std::string& path, std::vector<std::string>& names) {
    names.clear();
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) return false;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) names.emplace_back(data->nodes[i].name ? data->nodes[i].name : "");
    cgltf_free(data);
    return true;
}

bool loadGltfNodeTrs(const std::string& path, const std::vector<std::string>& names, std::vector<float>& trs,
                     std::string& err) {
    trs.clear();
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    // Node transforms are in the JSON: no buffers needed.
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) {
        err = "cannot parse " + path;
        return false;
    }
    bool same = data->nodes_count == names.size();
    for (cgltf_size i = 0; same && i < data->nodes_count; ++i)
        same = names[i] == (data->nodes[i].name ? data->nodes[i].name : "");
    if (!same) {
        cgltf_free(data);
        err = path + ": its nodes differ from the model's";
        return false;
    }
    trs.resize(data->nodes_count * 10);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) nodeTrs(data->nodes[i], &trs[i * 10], &trs[i * 10 + 3], &trs[i * 10 + 7]);
    cgltf_free(data);
    return true;
}

bool loadFrameMap(const std::string& path, FrameMap& out, std::string& err) {
    out = FrameMap{};
    Json j;
    if (!j.parse(path) || j.tok.empty() || j.tok[0].type != JSMN_OBJECT) {
        err = "cannot read " + path;
        return false;
    }
    if (const int d = j.get(0, "dirs"); d >= 0) out.dirs = std::max(1, int(j.num(d)));
    const int fr = j.get(0, "frames");
    if (fr < 0 || j.tok[std::size_t(fr)].type != JSMN_OBJECT) {
        err = path + ": no \"frames\"";
        return false;
    }
    int c = fr + 1;
    for (int m = 0; m < j.tok[std::size_t(fr)].size; ++m) {
        const int key = std::atoi(j.str(c).c_str());
        const int v = c + 1;
        c = j.skip(v);
        if (key < 0 || key > 4096 || j.tok[std::size_t(v)].type != JSMN_OBJECT) continue;
        FrameMapEntry e;
        if (const int a = j.get(v, "anim"); a >= 0) e.anim = j.str(a);
        if (const int f = j.get(v, "frame"); f >= 0) e.frame = j.num(f);
        if (const int y = j.get(v, "yaw"); y >= 0) e.yaw = j.num(y);
        if (const int sc = j.get(v, "scale"); sc >= 0) e.scale = j.num(sc);
        if (const int al = j.get(v, "alpha"); al >= 0) e.alpha = j.num(al);
        if (const int sh = j.get(v, "shift"); sh >= 0 && j.tok[std::size_t(sh)].type == JSMN_ARRAY && j.tok[std::size_t(sh)].size >= 2) {
            e.shiftE = j.num(sh + 1);
            e.shiftN = j.num(sh + 2);
        }
        if (e.anim.empty()) continue;
        if (out.frames.size() <= std::size_t(key)) out.frames.resize(std::size_t(key) + 1);
        out.frames[std::size_t(key)] = std::move(e);
    }
    return !out.frames.empty();
}

bool loadPoseSpec(const std::string& path, const std::string& key, std::vector<int>& animFrames, std::vector<int>& levels,
                  std::vector<int>& rows) {
    Json j;
    if (!j.parse(path) || j.tok.empty() || j.tok[0].type != JSMN_OBJECT) return false;
    if (const int k = j.get(0, "key"); k >= 0 && j.str(k) != key) return false;
    auto ints = [&](const char* name, std::vector<int>& v) {
        v.clear();
        const int a = j.get(0, name);
        if (a < 0 || j.tok[std::size_t(a)].type != JSMN_ARRAY) return;
        for (int m = 0; m < j.tok[std::size_t(a)].size; ++m) v.push_back(int(j.num(a + 1 + m)));
    };
    ints("anim_frames", animFrames);
    ints("levels", levels);
    ints("rows", rows);
    return true;
}

}  // namespace d2k::render3d
