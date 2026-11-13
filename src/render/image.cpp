#include "render/image.hpp"

#include "formats/io.hpp"
#include "formats/tga.hpp"

#include <SDL3/SDL.h>

#include <cctype>
#include <exception>
#include <filesystem>
#include <utility>

namespace d2k::render {

Texture::~Texture() {
    if (tex_) SDL_DestroyTexture(tex_);
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = std::exchange(o.tex_, nullptr);
        w_ = o.w_;
        h_ = o.h_;
    }
    return *this;
}

Texture Texture::load(SDL_Renderer* r, const std::string& dir, const std::string& name, bool linear, bool magentaKey) {
    Texture t;
    const std::string path = formats::findFileNoCase(dir, name);
    if (path.empty()) {
        SDL_Log("UI image %s not found", name.c_str());
        return t;
    }
    std::string ext = path.size() > 4 ? path.substr(path.size() - 4) : "";
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    try {
        if (ext == ".tga") {
            const auto img = formats::loadTga(path);
            t.tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
            if (t.tex_) SDL_UpdateTexture(t.tex_, nullptr, img.pixels.data(), img.width * 4);
            t.w_ = img.width;
            t.h_ = img.height;
        } else {
            if (SDL_Surface* s = SDL_LoadBMP(path.c_str())) {
                if (magentaKey) SDL_SetSurfaceColorKey(s, true, SDL_MapSurfaceRGB(s, 255, 0, 255));
                t.tex_ = SDL_CreateTextureFromSurface(r, s);
                t.w_ = s->w;
                t.h_ = s->h;
                SDL_DestroySurface(s);
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("UI image %s: %s", path.c_str(), e.what());
    }
    if (t.tex_) {
        SDL_SetTextureBlendMode(t.tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t.tex_, linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    } else {
        SDL_Log("UI image %s could not be loaded: %s", path.c_str(), SDL_GetError());
    }
    return t;
}

Texture Texture::loadFile(SDL_Renderer* r, const std::string& path, bool linear) {
    Texture t;
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return t;
    std::string ext = path.size() > 4 ? path.substr(path.size() - 4) : "";
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    SDL_Surface* s = ext == ".png" ? SDL_LoadPNG(path.c_str()) : SDL_LoadBMP(path.c_str());
    if (!s) return t;
    t.tex_ = SDL_CreateTextureFromSurface(r, s);
    t.w_ = s->w;
    t.h_ = s->h;
    SDL_DestroySurface(s);
    if (t.tex_) {
        SDL_SetTextureBlendMode(t.tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t.tex_, linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    return t;
}

}  // namespace d2k::render
