#pragma once
// "Photo terrain" (docs/3d-mode.md "Photo terrain", docs/art-pipeline.md "Photo terrain pack"; Reimagined only):
// real photographed ground - CC0 photogrammetry scans (Poly Haven) - instead of the procedural ripples, gravel,
// cracks and strata of the Reimagined surface. The pack (artpacks/photo-terrain/, built by
// tools/fetch_photo_terrain.py from tools/photo_terrain_manifest.json; not in git) holds per layer three
// block-compressed, mip-mapped textures, uploaded as they are into three texture arrays:
//   <role>_albedo.d2kt  BC1  colour (its large-scale differences flattened: repeats without blotches)
//   <role>_normal.d2kt  BC5  OpenGL normal map x, y
//   <role>_orh.d2kt     BC1  r ambient occlusion, g roughness, b height
// terrain3_fs samples them (photoTap: anti-tiled, two scales) and keeps the map's own palette: a scan gives
// colour / its mean, so only the detail is the photograph's. Without the pack (or with a file missing) create()
// returns nullptr and the terrain keeps its procedural look.

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace d2k::render3d {

class PhotoTerrain {
public:
    // Layers, in the arrays' order (the roles of tools/photo_terrain_manifest.json).
    enum Layer { kSand, kRipples, kGravel, kRock, kRough, kCliff, kConcrete, kLayers };

    // The pack folder (with every layer's files), "" when not installed. D2K_PHOTO_PACK overrides the search
    // (render::ArtPack::searchDirs() / "photo-terrain").
    static std::string packDir();
    static bool installed() { return !packDir().empty(); }
    static std::unique_ptr<PhotoTerrain> create(SDL_GPUDevice* dev, int aniso);
    ~PhotoTerrain();
    PhotoTerrain(const PhotoTerrain&) = delete;
    PhotoTerrain& operator=(const PhotoTerrain&) = delete;

    SDL_GPUTexture* albedo() const { return tex_[0]; }
    SDL_GPUTexture* normal() const { return tex_[1]; }
    SDL_GPUTexture* orh() const { return tex_[2]; }
    SDL_GPUSampler* sampler() const { return sampler_; }
    // F3U::photo[2 + layer] (terrain_v3_msl.hpp): rgb = 1 / the scan's mean colour, a = world px per repeat.
    const float* layerU(int layer) const { return layerU_[layer]; }

private:
    explicit PhotoTerrain(SDL_GPUDevice* dev) : dev_(dev) {}
    bool load(const std::string& dir, int aniso);
    SDL_GPUDevice* dev_;
    SDL_GPUTexture* tex_[3] = {nullptr, nullptr, nullptr};
    SDL_GPUSampler* sampler_ = nullptr;
    float layerU_[kLayers][4] = {};
};

}  // namespace d2k::render3d
