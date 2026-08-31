#pragma once

#include "Renderer.hpp"
#include "render/ElementRenderer.hpp"

#include <pixman.h>

namespace Render::Pixman {
    class CNoopSyncFDManager : public ISyncFDManager {
      public:
        CNoopSyncFDManager() = default;
    };

    class CHyprPixmanRenderer : public Render::IHyprRenderer {
      public:
        CHyprPixmanRenderer();
        ~CHyprPixmanRenderer() = default;

        eType                   type() override;
        void                    endRender(const std::function<void()>& renderingDoneCallback = {}) override;
        UP<ISyncFDManager>      createSyncFDManager() override;
        SP<ITexture>            createStencilTexture(const int width, const int height) override;
        SP<ITexture>            createTexture(bool opaque = false) override;
        SP<ITexture>            createTexture(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const Vector2D& size, bool keepDataCopy = false, bool opaque = false) override;
        SP<ITexture>            createTexture(const Aquamarine::SDMABUFAttrs&, bool opaque = false) override;
        SP<ITexture>            createTexture(const int width, const int height, unsigned char* const data) override;
        SP<ITexture>            createTexture(cairo_surface_t* cairo) override;
        SP<ITexture>            createTexture(std::span<const float> lut3D, size_t N) override;
        bool                    explicitSyncSupported() override;
        std::vector<SDRMFormat> getDRMFormats() override;
        std::vector<uint64_t>   getDRMFormatModifiers(DRMFormat format) override;
        SP<IFramebuffer>        createFB(const std::string& name = "") override;
        void                    disableScissor() override;
        void                    blend(bool enabled) override;
        void                    drawShadow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& color, float a) override;
        void drawShadow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& grad1, const Config::CGradientValueData& grad2, float lerp,
                        float a) override;
        void drawGlow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& color, float a) override;
        void drawGlow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& grad1, const Config::CGradientValueData& grad2, float lerp,
                      float a) override;
        SP<ITexture>         blurFramebuffer(SP<IFramebuffer> source, float a, CRegion* originalDamage) override;
        void                 setViewport(int x, int y, int width, int height) override;
        bool                 reloadShaders(const std::string& path = "") override;
        void                 bindFB(SP<IFramebuffer> fb) override;

        WP<IElementRenderer> elementRenderer() override;

        // the image draws currently composite into (owned by the bound framebuffer)
        pixman_image_t*      targetImage();
        bool                 blendEnabled();

      private:
        void                 renderOffToMain(SP<IFramebuffer> off) override;
        SP<IRenderbuffer>    getOrCreateRenderbufferInternal(SP<Aquamarine::IBuffer> buffer, uint32_t fmt) override;
        bool                 beginRenderInternal(PHLMONITOR pMonitor, CRegion& damage, bool simple = false) override;
        bool                 beginFullFakeRenderInternal(PHLMONITOR pMonitor, CRegion& damage, SP<IFramebuffer> fb, bool simple = false) override;
        void                 initRender() override;
        bool                 initRenderBuffer(SP<Aquamarine::IBuffer> buffer, uint32_t fmt) override;

        void                 begin(PHLMONITOR pMonitor, const CRegion& damage);
        bool                 saveBufferForMirror();

        SP<IRenderbuffer>    m_currentRenderbuffer;
        UP<IElementRenderer> m_elementRenderer;
        pixman_image_t*      m_targetImage = nullptr; // borrowed from the bound fb
        bool                 m_blend       = true;
        bool                 m_frameActive = false; // begin() ran for the current frame

        struct {
            CHyprSignalListener configReloaded;
        } m_listeners;
    };
}
