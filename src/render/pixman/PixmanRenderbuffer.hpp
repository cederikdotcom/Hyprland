#pragma once

#include "../Renderbuffer.hpp"

namespace Render::Pixman {
    class CPixmanRenderbuffer : public IRenderbuffer {
      public:
        CPixmanRenderbuffer(SP<Aquamarine::IBuffer> buffer, uint32_t format);
        ~CPixmanRenderbuffer();

        void bind() override;
        void unbind() override;

      private:
        uint32_t m_format = 0;
        uint8_t* m_lastDataPtr = nullptr;
        bool     m_mapped      = false;
    };
}
