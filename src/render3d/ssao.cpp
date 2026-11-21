#include "render3d/ssao.hpp"

#include "render3d/shader_blobs.hpp"

#include <algorithm>

namespace d2k::render3d {

namespace {
// Fragment uniform buffer 0 of ssao_fs / ssaoblur_fs (SsaoU in render3d/ssao_msl.hpp / shaders.hlsl).
struct SsaoFU {
    float proj[4];
    float tex[4];
    float k[4];
};
}  // namespace

Ssao::~Ssao() {
    release();
    for (SDL_GPUGraphicsPipeline* p : {aoPipe_, blurPipe_})
        if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
    for (SDL_GPUSampler* s : {nearest_, linear_})
        if (s) SDL_ReleaseGPUSampler(dev_, s);
}

void Ssao::release() {
    for (SDL_GPUTexture** t : {&depth_, &ao_[0], &ao_[1]}) {
        if (*t) SDL_ReleaseGPUTexture(dev_, *t);
        *t = nullptr;
    }
    w_ = h_ = 0;
}

bool Ssao::ensure(int w, int h, SDL_GPUTextureFormat depthFmt) {
    if (failed_) return false;
    const int hw = std::max(1, (w + 1) / 2), hh = std::max(1, (h + 1) / 2);
    if (depth_ && hw == w_ && hh == h_ && depthFmt == depthFmt_) return true;
    release();
    const SDL_GPUTextureUsageFlags colorUse = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    const SDL_GPUTextureUsageFlags depthUse = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!aoPipe_) {
        aoFmt_ = SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTURETYPE_2D, colorUse)
                     ? SDL_GPU_TEXTUREFORMAT_R8_UNORM
                     : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUShader* vs = createShader(dev_, ShaderId::FxcVs, 0, 0);
        SDL_GPUShader* fa = createShader(dev_, ShaderId::SsaoFs, 1, 1);
        SDL_GPUShader* fb = createShader(dev_, ShaderId::SsaoBlurFs, 2, 1);
        if (vs && fa && fb) {
            SDL_GPUColorTargetDescription ctd{};
            ctd.format = aoFmt_;
            SDL_GPUGraphicsPipelineCreateInfo pi{};
            pi.vertex_shader = vs;
            pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pi.target_info.color_target_descriptions = &ctd;
            pi.target_info.num_color_targets = 1;
            pi.fragment_shader = fa;
            aoPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
            pi.fragment_shader = fb;
            blurPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        }
        for (SDL_GPUShader* s : {vs, fa, fb})
            if (s) SDL_ReleaseGPUShader(dev_, s);
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nearest_ = SDL_CreateGPUSampler(dev_, &si);
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
        linear_ = SDL_CreateGPUSampler(dev_, &si);
    }
    const bool depthOk = SDL_GPUTextureSupportsFormat(dev_, depthFmt, SDL_GPU_TEXTURETYPE_2D, depthUse);
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.width = Uint32(hw);
    ci.height = Uint32(hh);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    if (aoPipe_ && blurPipe_ && nearest_ && linear_ && depthOk) {
        ci.format = depthFmt;
        ci.usage = depthUse;
        depth_ = SDL_CreateGPUTexture(dev_, &ci);
        ci.format = aoFmt_;
        ci.usage = colorUse;
        ao_[0] = SDL_CreateGPUTexture(dev_, &ci);
        ao_[1] = SDL_CreateGPUTexture(dev_, &ci);
    }
    if (!depth_ || !ao_[0] || !ao_[1]) {
        SDL_Log("3D: ambient occlusion not available (%s) - drawn without it", SDL_GetError());
        release();
        failed_ = true;
        return false;
    }
    depthFmt_ = depthFmt;
    w_ = hw;
    h_ = hh;
    return true;
}

void Ssao::compute(SDL_GPUCommandBuffer* cmd, const Params& p) {
    if (!depth_ || !aoPipe_ || !blurPipe_) return;
    const SsaoFU u{{p.tanX, p.tanY, p.zNear, p.zFar},
                   {1.0f / float(w_), 1.0f / float(h_), p.viewW, p.viewH},
                   {p.radius, p.strength, p.bias, p.reach}};
    const SDL_GPUViewport vpt{0, 0, p.viewW, p.viewH, 0, 1};
    const SDL_Rect sc{0, 0, int(p.viewW + 0.5f), int(p.viewH + 0.5f)};
    for (int i = 0; i < 2; ++i) {
        SDL_GPUColorTargetInfo cti{};
        cti.texture = ao_[i];
        cti.clear_color = SDL_FColor{1, 1, 1, 1};
        cti.load_op = SDL_GPU_LOADOP_CLEAR;
        cti.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &cti, 1, nullptr);
        if (!pass) return;
        SDL_SetGPUViewport(pass, &vpt);
        SDL_SetGPUScissor(pass, &sc);
        SDL_BindGPUGraphicsPipeline(pass, i == 0 ? aoPipe_ : blurPipe_);
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
        if (i == 0) {
            const SDL_GPUTextureSamplerBinding sb{depth_, nearest_};
            SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
        } else {
            const SDL_GPUTextureSamplerBinding sb[2] = {{ao_[0], nearest_}, {depth_, nearest_}};
            SDL_BindGPUFragmentSamplers(pass, 0, sb, 2);
        }
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
}

}  // namespace d2k::render3d
