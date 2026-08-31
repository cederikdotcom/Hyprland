#include "PixmanElementRenderer.hpp"
#include "PixmanTexture.hpp"
#include "PixmanFramebuffer.hpp"
#include "../PixmanRenderer.hpp"
#include "../Renderer.hpp"
#include "../../debug/log/Logger.hpp"
#include "../../helpers/math/Math.hpp"

#include <pixman.h>

using namespace Render;
using namespace Render::Pixman;

static CHyprPixmanRenderer* renderer() {
    // only ever instantiated by CHyprPixmanRenderer
    return sc<CHyprPixmanRenderer*>(g_pHyprRenderer.get());
}

static pixman_color_t pixmanColor(const CHyprColor& premult) {
    return pixman_color_t{
        .red   = sc<uint16_t>(std::clamp(premult.r, 0.0, 1.0) * 0xFFFF),
        .green = sc<uint16_t>(std::clamp(premult.g, 0.0, 1.0) * 0xFFFF),
        .blue  = sc<uint16_t>(std::clamp(premult.b, 0.0, 1.0) * 0xFFFF),
        .alpha = sc<uint16_t>(std::clamp(premult.a, 0.0, 1.0) * 0xFFFF),
    };
}

void CPixmanElementRenderer::fillRegion(const CRegion& region, const CHyprColor& premultColor, bool opSrc) {
    const auto TARGET = renderer()->targetImage();
    if (!TARGET || region.empty())
        return;

    auto       color = pixmanColor(premultColor);
    const auto FILL  = pixman_image_create_solid_fill(&color);
    if (!FILL)
        return;

    const auto EXTENT = region.copy().getExtents();

    pixman_image_set_clip_region32(TARGET, const_cast<CRegion&>(region).pixman());
    pixman_image_composite32(opSrc ? PIXMAN_OP_SRC : PIXMAN_OP_OVER, FILL, nullptr, TARGET, 0, 0, 0, 0, EXTENT.x, EXTENT.y, EXTENT.width, EXTENT.height);
    pixman_image_set_clip_region32(TARGET, nullptr);

    pixman_image_unref(FILL);
}

void CPixmanElementRenderer::draw(WP<CClearPassElement> element, const CRegion& damage) {
    const auto& color        = element->m_data.color;
    auto&       m_renderData = g_pHyprRenderer->m_renderData;
    RASSERT(m_renderData.pMonitor, "Tried to render without begin()!");

    const auto TARGET = renderer()->targetImage();
    if (!TARGET)
        return;

    CRegion clearRegion = {0, 0, pixman_image_get_width(TARGET), pixman_image_get_height(TARGET)};
    if (!m_renderData.damage.empty())
        clearRegion.intersect(m_renderData.damage);

    // a clear writes the raw color, no blending and no premultiplication (mirrors glClear)
    fillRegion(clearRegion, color, true);
}

void CPixmanElementRenderer::draw(WP<CRectPassElement> element, const CRegion& damage) {
    const auto& data         = element->m_data;
    auto&       m_renderData = g_pHyprRenderer->m_renderData;

    // base drawRect precomputed modifiedBox (renderModif applied)
    CBox box = data.modifiedBox;
    box.round();

    CRegion region = damage;
    region.intersect(box);
    if (!m_renderData.clipBox.empty())
        region.intersect(m_renderData.clipBox);
    if (region.empty())
        return;

    const CHyprColor PREMULT   = {data.color.r * data.color.a, data.color.g * data.color.a, data.color.b * data.color.a, data.color.a};
    const auto       CONVERTED = g_pHyprRenderer->getConvertedColor(PREMULT);

    fillRegion(region, CONVERTED, data.color.a >= 1.0);
}

void CPixmanElementRenderer::draw(WP<CBorderPassElement> element, const CRegion& damage) {
    const auto& data = element->m_data;

    if (data.hasGrad2 || data.grad1.m_colors.size() > 1) {
        static bool warned = false;
        if (!warned) {
            Log::logger->log(Log::WARN, "pixman: gradient borders are not supported, rendering the first stop as a solid color");
            warned = true;
        }
    }

    if (data.grad1.m_colors.empty() || data.borderSize < 1)
        return;

    CBox box = data.box;
    g_pHyprRenderer->m_renderData.renderModif.applyToBox(box);
    box.round();

    if (box.width < 1 || box.height < 1)
        return;

    // flat mode: rounding is 0, the border is a rectangular ring
    CRegion    ring = {box};

    const auto BS    = sc<double>(data.borderSize);
    CBox       inner = box;
    inner.x += BS;
    inner.y += BS;
    inner.width -= 2 * BS;
    inner.height -= 2 * BS;

    if (inner.width > 0 && inner.height > 0)
        ring.subtract(CRegion{inner});

    ring.intersect(damage);
    if (ring.empty())
        return;

    const auto&      COL     = data.grad1.m_colors[0];
    const auto       A       = COL.a * data.a;
    const CHyprColor PREMULT = {COL.r * A, COL.g * A, COL.b * A, A};

    fillRegion(ring, g_pHyprRenderer->getConvertedColor(PREMULT), A >= 1.0);
}

void CPixmanElementRenderer::drawTexture(SP<ITexture> tex, const CBox& box, const CRegion& clip, float alpha, bool useNearest) {
    const auto TARGET = renderer()->targetImage();
    const auto PTEX   = dynamicPointerCast<CPixmanTexture>(tex);

    if (!TARGET || !PTEX || !PTEX->image() || clip.empty())
        return;

    const auto  SRC = PTEX->image();

    const auto& m_renderData = g_pHyprRenderer->m_renderData;

    // texture transform (+ the monitor transform when enabled)
    auto        transform = tex->m_transform;
    if (g_pHyprRenderer->monitorTransformEnabled())
        transform = Math::composeTransform(Math::wlTransformToHyprutils(Math::invertTransform(m_renderData.pMonitor->m_transform)), transform);

    if (transform != HYPRUTILS_TRANSFORM_NORMAL) {
        static bool warned = false;
        if (!warned) {
            Log::logger->log(Log::WARN, "pixman: rendering a transformed texture (transform {}); this path is best-effort and unverified", sc<int>(transform));
            warned = true;
        }
    }

    // source crop, from the primary surface UV when the caller allows it
    CBox srcBox = {0, 0, tex->m_size.x, tex->m_size.y};
    if (m_renderData.primarySurfaceUVTopLeft != Vector2D(-1, -1)) {
        const auto& UVTL = m_renderData.primarySurfaceUVTopLeft;
        const auto& UVBR = m_renderData.primarySurfaceUVBottomRight;
        srcBox           = {UVTL.x * tex->m_size.x, UVTL.y * tex->m_size.y, (UVBR.x - UVTL.x) * tex->m_size.x, (UVBR.y - UVTL.y) * tex->m_size.y};
    }
    srcBox.round();

    CBox dstBox = box;
    dstBox.round();

    if (dstBox.width < 1 || dstBox.height < 1 || srcBox.width < 1 || srcBox.height < 1)
        return;

    pixman_image_t* mask = nullptr;
    if (alpha < 1.f) {
        pixman_color_t maskColor = {.red = 0, .green = 0, .blue = 0, .alpha = sc<uint16_t>(std::clamp(alpha, 0.f, 1.f) * 0xFFFF)};
        mask                     = pixman_image_create_solid_fill(&maskColor);
    }

    const auto OP = !mask && (tex->m_opaque || !renderer()->blendEnabled()) ? PIXMAN_OP_SRC : PIXMAN_OP_OVER;

    pixman_image_set_clip_region32(TARGET, const_cast<CRegion&>(clip).pixman());

    // rotate the source size into destination coordinates
    const bool FLIPPED   = transform >= HYPRUTILS_TRANSFORM_FLIPPED;
    const int  ROTATION  = transform % 4; // 0 = normal, 1 = 90, 2 = 180, 3 = 270
    const bool SWAPPED   = ROTATION % 2 == 1;
    const auto SRCW_TFMD = SWAPPED ? srcBox.height : srcBox.width;
    const auto SRCH_TFMD = SWAPPED ? srcBox.width : srcBox.height;

    if (transform == HYPRUTILS_TRANSFORM_NORMAL && srcBox.width == dstBox.width && srcBox.height == dstBox.height) {
        // fast path: straight blit with a source offset, no transform object -
        // keeps libpixman's SIMD fast paths hot
        pixman_image_set_transform(SRC, nullptr);
        pixman_image_composite32(OP, SRC, mask, TARGET, srcBox.x, srcBox.y, 0, 0, dstBox.x, dstBox.y, dstBox.width, dstBox.height);
    } else {
        // wlroots pixman pass recipe (render/pixman/pass.c): transforms apply to the
        // coordinate system, so read the steps bottom-up for what happens to the image
        int tr_cos = 1, tr_sin = 0, tr_x = 0, tr_y = 0;
        switch (ROTATION) {
            case 0: break;
            case 1:
                tr_cos = 0;
                tr_sin = 1;
                tr_y   = srcBox.width;
                break;
            case 2:
                tr_cos = -1;
                tr_sin = 0;
                tr_x   = srcBox.width;
                tr_y   = srcBox.height;
                break;
            case 3:
                tr_cos = 0;
                tr_sin = -1;
                tr_x   = srcBox.height;
                break;
            default: break;
        }

        pixman_transform_t t;
        pixman_transform_init_identity(&t);

        // scale to the dst box size (post-rotation, hence the transformed src size)
        pixman_transform_scale(&t, nullptr, pixman_double_to_fixed(SRCW_TFMD / sc<double>(dstBox.width)), pixman_double_to_fixed(SRCH_TFMD / sc<double>(dstBox.height)));

        // translate the rotated result back to the origin
        pixman_transform_translate(&t, nullptr, -pixman_int_to_fixed(tr_x), -pixman_int_to_fixed(tr_y));

        // rotation
        pixman_transform_rotate(&t, nullptr, pixman_int_to_fixed(tr_cos), pixman_int_to_fixed(tr_sin));

        // flip before rotation
        if (FLIPPED) {
            pixman_transform_translate(&t, nullptr, -pixman_int_to_fixed(srcBox.width), pixman_int_to_fixed(0));
            pixman_transform_scale(&t, nullptr, pixman_int_to_fixed(-1), pixman_int_to_fixed(1));
        }

        // source crop offset, applied first
        pixman_transform_translate(&t, nullptr, pixman_int_to_fixed(srcBox.x), pixman_int_to_fixed(srcBox.y));

        pixman_image_set_transform(SRC, &t);

        if (useNearest) {
            pixman_image_set_filter(SRC, PIXMAN_FILTER_NEAREST, nullptr, 0);
        } else {
            pixman_image_set_repeat(SRC, PIXMAN_REPEAT_PAD);
            pixman_image_set_filter(SRC, PIXMAN_FILTER_BILINEAR, nullptr, 0);
        }

        pixman_image_composite32(OP, SRC, mask, TARGET, 0, 0, 0, 0, dstBox.x, dstBox.y, dstBox.width, dstBox.height);

        // reset so pixman's cached fast paths stay hot for the next draw
        pixman_image_set_transform(SRC, nullptr);
        pixman_image_set_repeat(SRC, PIXMAN_REPEAT_NONE);
        pixman_image_set_filter(SRC, PIXMAN_FILTER_NEAREST, nullptr, 0);
    }

    pixman_image_set_clip_region32(TARGET, nullptr);

    if (mask)
        pixman_image_unref(mask);
}

void CPixmanElementRenderer::draw(WP<CTexPassElement> element, const CRegion& damage) {
    const auto& data         = element->m_data;
    auto&       m_renderData = g_pHyprRenderer->m_renderData;

    if (!data.tex)
        return;

    CBox box = data.box;
    m_renderData.renderModif.applyToBox(box);

    // damage selection mirrors the GL element renderer
    CRegion clip = data.damage.empty() ? damage : data.damage;
    clip.intersect(box);
    if (!m_renderData.clipBox.empty())
        clip.intersect(m_renderData.clipBox);
    if (!data.clipRegion.empty())
        clip.intersect(data.clipRegion);

    if (clip.empty())
        return;

    const bool CUSTOMUV = data.allowCustomUV && m_renderData.primarySurfaceUVTopLeft != Vector2D(-1, -1);
    if (!CUSTOMUV) {
        // drawTexture reads the UV from renderData; block it off for draws that disallow it
        const auto SAVETL                        = m_renderData.primarySurfaceUVTopLeft;
        const auto SAVEBR                        = m_renderData.primarySurfaceUVBottomRight;
        m_renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
        m_renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
        drawTexture(data.tex, box, clip, data.a, m_renderData.useNearestNeighbor);
        m_renderData.primarySurfaceUVTopLeft     = SAVETL;
        m_renderData.primarySurfaceUVBottomRight = SAVEBR;
    } else
        drawTexture(data.tex, box, clip, data.a, m_renderData.useNearestNeighbor);
}

void CPixmanElementRenderer::draw(WP<CFramebufferElement> element, const CRegion& damage) {
    Log::logger->log(Log::ERR, "Deprecated CFramebufferElement. Use g_pHyprRenderer->m_renderData and CTexPassElement instead");
}

void CPixmanElementRenderer::draw(WP<CPreBlurElement> element, const CRegion& damage) {
    ; // no-op: blur is unsupported on the pixman renderer (flat mode)
}

void CPixmanElementRenderer::draw(WP<CShadowPassElement> element, const CRegion& damage) {
    ; // no-op: shadows are unsupported on the pixman renderer (flat mode)
}

void CPixmanElementRenderer::draw(WP<CInnerGlowPassElement> element, const CRegion& damage) {
    ; // no-op: glow is unsupported on the pixman renderer (flat mode)
}

void CPixmanElementRenderer::draw(WP<CTextureMatteElement> element, const CRegion& damage) {
    ; // no-op: mattes are only used by blur / snapshot effects, dead in flat mode
}
