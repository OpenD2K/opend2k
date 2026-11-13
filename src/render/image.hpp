#pragma once
// GPU textures from the menu artwork in data/UI_GFX (.TGA via formats/tga, .BMP via SDL).

#include <string>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::render {

class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(Texture&& o) noexcept { *this = std::move(o); }
    Texture& operator=(Texture&& o) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Loads `name` from `dir` (case-insensitive). Returns an empty texture on failure.
    // `magentaKey`: pure magenta (255, 0, 255) pixels of a .BMP become transparent (the
    // original UI's colour key for its 8-bit pictures).
    static Texture load(SDL_Renderer* r, const std::string& dir, const std::string& name, bool linear = true,
                        bool magentaKey = false);

    // Loads a .png or .bmp file by its full path; empty (and silent) when it does not exist.
    static Texture loadFile(SDL_Renderer* r, const std::string& path, bool linear = true);

    SDL_Texture* get() const { return tex_; }
    explicit operator bool() const { return tex_ != nullptr; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
};

}  // namespace d2k::render
