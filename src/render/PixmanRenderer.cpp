#include "PixmanRenderer.hpp"

#include "../config/ConfigValue.hpp"
#include "../debug/log/Logger.hpp"
#include "../event/EventBus.hpp"
#include "../output/Monitor.hpp"
#include "Renderer.hpp"
#include "./pixman/PixmanElementRenderer.hpp"
#include "./pixman/PixmanFormat.hpp"
#include "./pixman/PixmanFramebuffer.hpp"
#include "./pixman/PixmanRenderbuffer.hpp"
#include "./pixman/PixmanTexture.hpp"

#include <aquamarine/output/Output.hpp>
#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <hyprutils/utils/ScopeGuard.hpp>

using namespace Hyprutils::Utils;
using namespace Render;
using namespace Render::Pixman;

static void warnFlatModeViolations() {
    static auto PANIMATIONS   = CConfigValue<Config::INTEGER>("animations:enabled");
    static auto PBLUR         = CConfigValue<Config::INTEGER>("decoration:blur:enabled");
    static auto PROUNDING     = CConfigValue<Config::INTEGER>("decoration:rounding");
    static auto PSHADOW       = CConfigValue<Config::INTEGER>("decoration:shadow:enabled");
    static auto PSHADER       = CConfigValue<std::string>("decoration:screen_shader");
    static auto PDAMAGETRACK  = CConfigValue<Config::INTEGER>("debug:damage_tracking");

    std::string offenders;
    if (*PANIMATIONS)
        offenders += "animations:enabled ";
    if (*PBLUR)
        offenders += "decoration:blur:enabled ";
    if (*PROUNDING)
        offenders += "decoration:rounding ";
    if (*PSHADOW)
        offenders += "decoration:shadow:enabled ";
    if (!(*PSHADER).empty())
        offenders += "decoration:screen_shader ";
    if (*PDAMAGETRACK != 2 /* full */)
        offenders += "debug:damage_tracking ";

    if (!offenders.empty())
        Log::logger->log(Log::WARN,
                         "pixman renderer: flat mode only: the following config options are set but unsupported and will render without their effect(s): {}(set them to their "
                         "flat-mode values to silence this)",
                         offenders);
}

CHyprPixmanRenderer::CHyprPixmanRenderer() : IHyprRenderer(), m_elementRenderer(makeUnique<CPixmanElementRenderer>()) {
    m_software = true;

    Log::logger->log(Log::INFO, "pixman renderer: software (cpu) rendering, flat mode only; effects (blur, shadows, rounding, screen shaders, CM) are disabled");

    warnFlatModeViolations();
    m_listeners.configReloaded = Event::bus()->m_events.config.reloaded.listen([] { warnFlatModeViolations(); });
}

IHyprRenderer::eType CHyprPixmanRenderer::type() {
    return RT_PIXMAN;
}

void CHyprPixmanRenderer::initRender() {
    ; // no context to make current
}

bool CHyprPixmanRenderer::initRenderBuffer(SP<Aquamarine::IBuffer> buffer, uint32_t fmt) {
    try {
        m_currentRenderbuffer = getOrCreateRenderbuffer(buffer, fmt);
    } catch (std::exception& e) {
        Log::logger->log(Log::ERR, "getOrCreateRenderbuffer failed for {}", NFormatUtils::drmFormatName(fmt));
        return false;
    }

    return m_currentRenderbuffer && m_currentRenderbuffer->good();
}

void CHyprPixmanRenderer::begin(PHLMONITOR pMonitor, const CRegion& damage) {
    m_renderData.pMonitor = pMonitor;

    if (pMonitor->m_transform != WL_OUTPUT_TRANSFORM_NORMAL) {
        static bool warned = false;
        if (!warned) {
            Log::logger->log(Log::WARN, "pixman renderer: output {} has a non-normal transform; transformed outputs are unsupported and will likely render incorrectly",
                             pMonitor->m_name);
            warned = true;
        }
    }

    m_renderData.transformDamage = true;
    m_renderData.damage.set(damage);
    m_renderData.finalDamage.set(damage);

    const auto DMGEXT = m_renderData.damage.copy().getExtents();
    Log::logger->log(Log::TRACE, "pixman: begin frame on {}: damage {} rect(s), extents {},{} {}x{}", pMonitor->m_name, pixman_region32_n_rects(m_renderData.damage.pixman()),
                     DMGEXT.x, DMGEXT.y, DMGEXT.width, DMGEXT.height);

    // we render straight into the target buffer: main == out == current
    m_renderData.mainFB = m_renderData.currentFB;
    m_renderData.outFB  = m_renderData.currentFB;

    if (m_renderData.currentFB)
        m_renderData.currentFB->setImageDescription(pMonitor->workBufferImageDescription());

    pushMonitorTransformEnabled(false);
}

bool CHyprPixmanRenderer::beginRenderInternal(PHLMONITOR pMonitor, CRegion& damage, bool simple) {
    if (!m_currentRenderbuffer)
        return false;

    m_currentRenderbuffer->bind(); // maps the buffer and binds its fb (sets the target image)

    if (!m_targetImage) {
        Log::logger->log(Log::ERR, "pixman renderer: no target image after binding the renderbuffer");
        return false;
    }

    begin(pMonitor, damage);
    return true;
}

bool CHyprPixmanRenderer::beginFullFakeRenderInternal(PHLMONITOR pMonitor, CRegion& damage, SP<IFramebuffer> fb, bool simple) {
    RASSERT(fb, "Cannot render FULL_FAKE without a provided fb!");

    bindFB(fb);

    if (!m_targetImage) {
        Log::logger->log(Log::ERR, "pixman renderer: FULL_FAKE render with an unallocated fb");
        return false;
    }

    begin(pMonitor, damage);
    m_renderData.outFB = fb;
    return true;
}

void CHyprPixmanRenderer::bindFB(SP<IFramebuffer> fb) {
    m_renderData.currentFB = fb;

    if (!fb) {
        m_targetImage = nullptr;
        return;
    }

    fb->bind();

    const auto PIXFB = dynamicPointerCast<CPixmanFramebuffer>(fb);
    m_targetImage    = PIXFB ? PIXFB->image() : nullptr;

    if (!m_targetImage)
        Log::logger->log(Log::ERR, "pixman renderer: bindFB on a non-pixman or unallocated fb \"{}\"", fb ? "?" : "null");
}

void CHyprPixmanRenderer::endRender(const std::function<void()>& renderingDoneCallback) {
    const auto PMONITOR = m_renderData.pMonitor;

    m_renderData.damage = m_renderPass.render(m_renderData.damage);

    // keep the monitor mirror fb fresh for screencopy / mirrors (GL does this in its end-blit)
    if (m_renderMode == RENDER_MODE_NORMAL && PMONITOR && PMONITOR->needsACopyFB()) {
        if (saveBufferForMirror())
            PMONITOR->resources()->markMirrorFBUpdated();
        else
            PMONITOR->resources()->invalidateMirrorFB();
    }

    auto cleanup = CScopeGuard([this]() {
        if (m_currentRenderbuffer)
            m_currentRenderbuffer->unbind();
        m_currentRenderbuffer = nullptr;
        m_currentBuffer       = nullptr;
        m_targetImage         = nullptr;
    });

    m_renderData.currentWindow.reset();
    m_renderData.surface.reset();
    m_renderData.clipBox = {};

    if (m_renderMode != RENDER_MODE_TO_BUFFER_READ_ONLY) {
        // rendered directly into the target buffer; cpu writes are already there,
        // nothing to blit or flush. Report the full frame damage.
        m_renderData.damage            = m_renderData.finalDamage;
        m_renderData.mouseZoomFactor   = 1.f;
        m_renderData.mouseZoomUseMouse = true;
        m_renderData.blockScreenShader = false;
        m_renderData.currentFB.reset();
        m_renderData.mainFB.reset();
        m_renderData.outFB.reset();
        popMonitorTransformEnabled();
        m_renderData.pMonitor.reset();
    } else {
        m_renderData.pMonitor.reset();
        m_renderData.mouseZoomFactor   = 1.f;
        m_renderData.mouseZoomUseMouse = true;
    }

    if (m_renderMode == RENDER_MODE_FULL_FAKE)
        return;

    if (m_renderMode == RENDER_MODE_NORMAL)
        PMONITOR->m_output->state->setBuffer(m_currentBuffer);

    // no fences on a cpu renderer: once the pass ran, the writes are done
    m_usedAsyncBuffers.clear();
    if (renderingDoneCallback)
        renderingDoneCallback();
}

bool CHyprPixmanRenderer::saveBufferForMirror() {
    if (!m_targetImage || !m_renderData.pMonitor)
        return false;

    const auto FB = dynamicPointerCast<CPixmanFramebuffer>(m_renderData.pMonitor->resources()->mirrorFB());
    if (!FB || !FB->image()) {
        Log::logger->log(Log::ERR, "pixman renderer: invalid mirror fb");
        return false;
    }

    pixman_image_set_clip_region32(FB->image(), m_renderData.finalDamage.pixman());
    pixman_image_composite32(PIXMAN_OP_SRC, m_targetImage, nullptr, FB->image(), 0, 0, 0, 0, 0, 0, pixman_image_get_width(FB->image()), pixman_image_get_height(FB->image()));
    pixman_image_set_clip_region32(FB->image(), nullptr);

    return true;
}

void CHyprPixmanRenderer::renderOffToMain(SP<IFramebuffer> off) {
    const auto OFF = dynamicPointerCast<CPixmanFramebuffer>(off);
    if (!OFF || !OFF->image() || !m_targetImage)
        return;

    pixman_image_set_clip_region32(m_targetImage, m_renderData.finalDamage.pixman());
    pixman_image_composite32(PIXMAN_OP_SRC, OFF->image(), nullptr, m_targetImage, 0, 0, 0, 0, 0, 0, pixman_image_get_width(OFF->image()), pixman_image_get_height(OFF->image()));
    pixman_image_set_clip_region32(m_targetImage, nullptr);
}

SP<IRenderbuffer> CHyprPixmanRenderer::getOrCreateRenderbufferInternal(SP<Aquamarine::IBuffer> buffer, uint32_t fmt) {
    return makeShared<CPixmanRenderbuffer>(buffer, fmt);
}

UP<ISyncFDManager> CHyprPixmanRenderer::createSyncFDManager() {
    return makeUnique<CNoopSyncFDManager>();
}

SP<ITexture> CHyprPixmanRenderer::createStencilTexture(const int width, const int height) {
    // stencils only serve rounding / blur, dead in flat mode
    return makeShared<CPixmanTexture>();
}

SP<ITexture> CHyprPixmanRenderer::createTexture(bool opaque) {
    return makeShared<CPixmanTexture>(opaque);
}

SP<ITexture> CHyprPixmanRenderer::createTexture(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const Vector2D& size, bool keepDataCopy, bool opaque) {
    return makeShared<CPixmanTexture>(drmFormat, pixels, stride, size, keepDataCopy, opaque);
}

SP<ITexture> CHyprPixmanRenderer::createTexture(const Aquamarine::SDMABUFAttrs& attrs, bool opaque) {
    // unreachable: getDRMFormats() is empty, so linux-dmabuf is never advertised
    Log::logger->log(Log::ERR, "pixman renderer: refusing to create a dmabuf texture (dmabuf is unsupported)");
    return nullptr;
}

SP<ITexture> CHyprPixmanRenderer::createTexture(const int width, const int height, unsigned char* const data) {
    // data is cairo ARGB32: premultiplied, byte-identical to PIXMAN_a8r8g8b8 (LE)
    return makeShared<CPixmanTexture>(DRM_FORMAT_ARGB8888, data, sc<uint32_t>(width) * 4, Vector2D{width, height});
}

SP<ITexture> CHyprPixmanRenderer::createTexture(cairo_surface_t* cairo) {
    const auto CAIROFORMAT = cairo_image_surface_get_format(cairo);
    const auto W           = cairo_image_surface_get_width(cairo);
    const auto H           = cairo_image_surface_get_height(cairo);
    const auto STRIDE      = sc<uint32_t>(cairo_image_surface_get_stride(cairo));
    const auto DATA        = cairo_image_surface_get_data(cairo);

    // cairo is pixman underneath: ARGB32 == PIXMAN_a8r8g8b8 (premultiplied), RGB24 == x8r8g8b8
    uint32_t   drmFormat = 0;
    switch (CAIROFORMAT) {
        case CAIRO_FORMAT_ARGB32: drmFormat = DRM_FORMAT_ARGB8888; break;
        case CAIRO_FORMAT_RGB24: drmFormat = DRM_FORMAT_XRGB8888; break;
        default: {
            Log::logger->log(Log::ERR, "pixman renderer: unsupported cairo surface format {}", sc<int>(CAIROFORMAT));
            return makeShared<CPixmanTexture>();
        }
    }

    return makeShared<CPixmanTexture>(drmFormat, DATA, STRIDE, Vector2D{W, H});
}

SP<ITexture> CHyprPixmanRenderer::createTexture(std::span<const float> lut3D, size_t N) {
    // CM LUTs are unsupported on the pixman renderer
    return makeShared<CPixmanTexture>();
}

bool CHyprPixmanRenderer::explicitSyncSupported() {
    return false;
}

std::vector<SDRMFormat> CHyprPixmanRenderer::getDRMFormats() {
    // empty: linux-dmabuf + MesaDRM are never advertised, clients fall back to wl_shm
    return {};
}

std::vector<uint64_t> CHyprPixmanRenderer::getDRMFormatModifiers(DRMFormat format) {
    return {};
}

SP<IFramebuffer> CHyprPixmanRenderer::createFB(const std::string& name) {
    return makeShared<CPixmanFramebuffer>(name);
}

void CHyprPixmanRenderer::disableScissor() {
    ; // clips are set and cleared per draw op
}

void CHyprPixmanRenderer::blend(bool enabled) {
    m_blend = enabled;
}

void CHyprPixmanRenderer::drawShadow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& color, float a) {
    ; // no-op: flat mode
}

void CHyprPixmanRenderer::drawShadow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& grad1, const Config::CGradientValueData& grad2,
                                     float lerp, float a) {
    ; // no-op: flat mode
}

void CHyprPixmanRenderer::drawGlow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& color, float a) {
    ; // no-op: flat mode
}

void CHyprPixmanRenderer::drawGlow(const CBox& box, int round, float roundingPower, int range, const Config::CGradientValueData& grad1, const Config::CGradientValueData& grad2,
                                   float lerp, float a) {
    ; // no-op: flat mode
}

SP<ITexture> CHyprPixmanRenderer::blurFramebuffer(SP<IFramebuffer> source, float a, CRegion* originalDamage) {
    // flat mode never blurs; belt and braces: return the source unmodified
    return source ? source->getTexture() : nullptr;
}

void CHyprPixmanRenderer::setViewport(int x, int y, int width, int height) {
    ; // no viewport state: draws are clamped by the target image size and clips
}

bool CHyprPixmanRenderer::reloadShaders(const std::string& path) {
    return false;
}

WP<IElementRenderer> CHyprPixmanRenderer::elementRenderer() {
    return m_elementRenderer;
}

pixman_image_t* CHyprPixmanRenderer::targetImage() {
    return m_targetImage;
}

bool CHyprPixmanRenderer::blendEnabled() {
    return m_blend;
}
