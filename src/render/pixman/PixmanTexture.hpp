#pragma once

#include "../Texture.hpp"

#include <pixman.h>
#include <vector>

namespace Render::Pixman {

    class CPixmanTexture : public ITexture {
      public:
        CPixmanTexture(CPixmanTexture&)        = delete;
        CPixmanTexture(CPixmanTexture&&)       = delete;
        CPixmanTexture(const CPixmanTexture&&) = delete;
        CPixmanTexture(const CPixmanTexture&)  = delete;

        CPixmanTexture(bool opaque = false);
        // copies the pixels into an owned store (Hyprland releases client shm buffers right after commit)
        CPixmanTexture(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const Vector2D& size, bool keepDataCopy = false, bool opaque = false);
        // wraps (refs) an existing image, does not own the backing store. Used by CPixmanFramebuffer.
        CPixmanTexture(pixman_image_t* image);
        ~CPixmanTexture();

        void            allocate(const Vector2D& size, uint32_t drmFormat = 0) override;
        void            update(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const CRegion& damage) override;
        void            setTexParameter(GLenum pname, GLint param) override;
        bool            ok() override;

        pixman_image_t* image();

      private:
        void                 destroyImage();
        bool                 writePixels(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const CRegion& damage);

        pixman_image_t*      m_image = nullptr;
        std::vector<uint8_t> m_pixels; // empty when wrapping an external image
        uint32_t             m_stride = 0;
    };
}
