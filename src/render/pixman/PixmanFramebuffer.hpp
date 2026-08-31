#pragma once

#include "../../defines.hpp"
#include "../Framebuffer.hpp"
#include "PixmanTexture.hpp"

#include <pixman.h>
#include <vector>

namespace Render::Pixman {
    class CPixmanFramebuffer : public IFramebuffer {
      public:
        CPixmanFramebuffer();
        CPixmanFramebuffer(const std::string& name);
        ~CPixmanFramebuffer();

        void            addStencil(SP<ITexture> tex) override;
        void            release() override;
        bool            readPixels(CHLBufferReference buffer, uint32_t offsetX = 0, uint32_t offsetY = 0, uint32_t width = 0, uint32_t height = 0) override;

        void            bind() override;

        // wrap an externally mapped pixel store (an output buffer); does not own the pixels
        bool            wrapExternal(uint8_t* data, uint32_t drmFormat, const Vector2D& size, uint32_t stride);
        void            unwrapExternal();

        pixman_image_t* image();

      protected:
        bool internalAlloc(int w, int h, DRMFormat format = DRM_FORMAT_ARGB8888) override;

      private:
        pixman_image_t*      m_image = nullptr; // owned reference; m_tex shares it
        std::vector<uint8_t> m_pixels;          // empty when wrapping an external mapping
        bool                 m_external = false;

        friend class CPixmanRenderbuffer;
    };
}
