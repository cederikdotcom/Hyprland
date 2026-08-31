#pragma once

#include "../ElementRenderer.hpp"

namespace Render::Pixman {
    class CPixmanElementRenderer : public IElementRenderer {
      public:
        CPixmanElementRenderer()          = default;
        virtual ~CPixmanElementRenderer() = default;

      protected:
        void draw(WP<CBorderPassElement> element, const CRegion& damage) override;
        void draw(WP<CClearPassElement> element, const CRegion& damage) override;
        void draw(WP<CFramebufferElement> element, const CRegion& damage) override;
        void draw(WP<CPreBlurElement> element, const CRegion& damage) override;
        void draw(WP<CRectPassElement> element, const CRegion& damage) override;
        void draw(WP<CShadowPassElement> element, const CRegion& damage) override;
        void draw(WP<CInnerGlowPassElement> element, const CRegion& damage) override;
        void draw(WP<CTexPassElement> element, const CRegion& damage) override;
        void draw(WP<CTextureMatteElement> element, const CRegion& damage) override;

      private:
        void fillRegion(const CRegion& region, const CHyprColor& premultColor, bool opSrc);
        void drawTexture(SP<ITexture> tex, const CBox& box, const CRegion& clip, float alpha, bool useNearest);
    };
}
