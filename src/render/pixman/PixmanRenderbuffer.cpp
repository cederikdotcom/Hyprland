#include "PixmanRenderbuffer.hpp"
#include "PixmanFramebuffer.hpp"
#include "../Renderer.hpp"
#include "../../Compositor.hpp"
#include "../../debug/log/Logger.hpp"

#include <aquamarine/buffer/Buffer.hpp>

using namespace Render;
using namespace Render::Pixman;

CPixmanRenderbuffer::CPixmanRenderbuffer(SP<Aquamarine::IBuffer> buffer, uint32_t format) : IRenderbuffer(buffer, format), m_format(format) {
    if (!(buffer->caps() & Aquamarine::eBufferCapability::BUFFER_CAPABILITY_DATAPTR)) {
        Log::logger->log(Log::ERR, "pixman: cannot create a renderbuffer: buffer has no data ptr capability");
        return;
    }

    m_framebuffer = makeShared<CPixmanFramebuffer>();

    m_listeners.destroyBuffer = buffer->events.destroy.listen([this] { g_pHyprRenderer->onRenderbufferDestroy(this); });

    m_good = true;
}

CPixmanRenderbuffer::~CPixmanRenderbuffer() {
    if (!g_pCompositor || g_pCompositor->m_isShuttingDown || !g_pHyprRenderer)
        return;

    unbind();
}

void CPixmanRenderbuffer::bind() {
    const auto BUFFER = m_hlBuffer.lock();
    if (!BUFFER) {
        Log::logger->log(Log::ERR, "pixman: renderbuffer bind: the aq buffer is gone");
        return;
    }

    auto [data, fmt, len] = BUFFER->beginDataPtr(0);
    if (!data) {
        Log::logger->log(Log::ERR, "pixman: renderbuffer bind: beginDataPtr failed");
        return;
    }

    m_mapped = true;

    const auto FB = dynamicPointerCast<CPixmanFramebuffer>(m_framebuffer);

    // (re)wrap if the mapping moved or the fb was never wrapped
    if (!FB->image() || data != m_lastDataPtr) {
        uint32_t   drmFormat = m_format;
        uint32_t   stride    = 0;

        const auto SHM = BUFFER->shm();
        if (SHM.success) {
            drmFormat = SHM.format;
            stride    = SHM.stride;
        } else if (const auto DMA = BUFFER->dmabuf(); DMA.success) {
            drmFormat = DMA.format;
            stride    = DMA.strides[0];
        } else {
            Log::logger->log(Log::ERR, "pixman: renderbuffer bind: buffer has neither shm nor dmabuf attrs");
            BUFFER->endDataPtr();
            m_mapped = false;
            return;
        }

        if (!FB->wrapExternal(data, drmFormat, BUFFER->size, stride)) {
            BUFFER->endDataPtr();
            m_mapped = false;
            return;
        }

        m_lastDataPtr = data;
    }

    g_pHyprRenderer->bindFB(m_framebuffer);
}

void CPixmanRenderbuffer::unbind() {
    if (!m_mapped)
        return;

    m_mapped = false;

    if (const auto BUFFER = m_hlBuffer.lock())
        BUFFER->endDataPtr();
}
