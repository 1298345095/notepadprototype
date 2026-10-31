// FLTK supplies the Wayland Cairo canvas. Use the same Pango settings for glyph
// advances in the fork's LineLayout and for drawing that text on the canvas.
#include "fork_editor.hpp"
#include <pango/pangocairo.h>
#include <cmath>
#include <numbers>

namespace Scintilla::Internal {
namespace {
class PangoFont final : public Font {
public:
    PangoFontDescription *description = pango_font_description_new();
    explicit PangoFont(const FontParameters &fp) {
        pango_font_description_set_family(description, fp.faceName);
        pango_font_description_set_absolute_size(description, fp.size * PANGO_SCALE);
        pango_font_description_set_weight(description, static_cast<PangoWeight>(fp.weight));
        pango_font_description_set_style(description, fp.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    }
    ~PangoFont() override { pango_font_description_free(description); }
};

class Canvas final : public Surface {
    cairo_t *cr = nullptr;
    PangoContext *context = pango_font_map_create_context(pango_cairo_font_map_get_default());
    PangoLayout *layout = pango_layout_new(context);
    void textLayout(const Font *font, std::string_view s) {
        pango_layout_set_font_description(layout, static_cast<const PangoFont *>(font)->description);
        pango_layout_set_text(layout, s.empty() ? "" : s.data(), static_cast<int>(s.size()));
    }
    void colour(ColourRGBA c) { cairo_set_source_rgba(cr, c.GetRedComponent(), c.GetGreenComponent(), c.GetBlueComponent(), c.GetAlphaComponent()); }
    void stroke(Stroke s) { colour(s.colour); cairo_set_line_width(cr, s.width); cairo_stroke(cr); }
    void fillStroke(FillStroke fs) { colour(fs.fill.colour); cairo_fill_preserve(cr); stroke(fs.stroke); }
    void rect(PRectangle r) { cairo_rectangle(cr, r.left, r.top, r.Width(), r.Height()); }
    void poly(const Point *p, size_t n) {
        if (!n) return;
        cairo_move_to(cr,p[0].x,p[0].y);
        for (size_t i=1;i<n;++i) cairo_line_to(cr,p[i].x,p[i].y);
    }
    void makeImage(int w, int h) {
        Release();
        auto *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,std::max(1,w),std::max(1,h));
        cr = cairo_create(image); cairo_surface_destroy(image);
    }
public:
    Canvas() { pango_layout_set_single_paragraph_mode(layout, TRUE); pango_layout_set_auto_dir(layout, FALSE); }
    ~Canvas() override { Release(); g_object_unref(layout); g_object_unref(context); }
    void Init(WindowID) noexcept override { makeImage(1,1); }
    void Init(SurfaceID sid, WindowID, bool) noexcept override { Release(); cr = cairo_reference(static_cast<cairo_t *>(sid)); }
    std::unique_ptr<Surface> AllocatePixMap(int w,int h) override {
        auto image = std::make_unique<Canvas>(); image->makeImage(w,h); return image;
    }
    void SetMode(SurfaceMode) noexcept override {}
    void SetRenderingParams(void *,void *) noexcept override {}
    void Release() noexcept override { if (cr) { cairo_destroy(cr); cr=nullptr; } }
    bool SupportsFeature(Supports f) const noexcept override { return f == Supports::FractionalStrokeWidth || f == Supports::TranslucentStroke; }
    bool Initialised() const noexcept override { return cr; }
    int LogPixelsY() const noexcept override { return 96; }
    int PixelDivisions() const noexcept override { return 1; }
    int DeviceHeightFont(int points) const noexcept override { return (points*96+36)/72; }
    void LineDraw(Point a,Point b,Stroke s) override { cairo_move_to(cr,a.x,a.y); cairo_line_to(cr,b.x,b.y); stroke(s); }
    void PolyLine(const Point *p,size_t n,Stroke s) override { poly(p,n); stroke(s); }
    void Polygon(const Point *p,size_t n,FillStroke fs) override { poly(p,n); cairo_close_path(cr); fillStroke(fs); }
    void RectangleDraw(PRectangle r,FillStroke s) override { rect(r); fillStroke(s); }
    void RectangleFrame(PRectangle r, Stroke s) override { rect(r); stroke(s); }
    void FillRectangle(PRectangle r, Fill f) override { rect(r); colour(f.colour); cairo_fill(cr); }
    void FillRectangleAligned(PRectangle r, Fill f) override { FillRectangle(r,f); }
    void FillRectangle(PRectangle r,Surface &pattern) override {
        auto *tile=cairo_pattern_create_for_surface(cairo_get_target(static_cast<Canvas &>(pattern).cr));
        cairo_pattern_set_extend(tile,CAIRO_EXTEND_REPEAT); cairo_set_source(cr,tile); rect(r); cairo_fill(cr); cairo_pattern_destroy(tile);
    }
    void RoundedRectangle(PRectangle r,FillStroke s) override { AlphaRectangle(r, 3,s); }
    void AlphaRectangle(PRectangle r,XYPOSITION radius,FillStroke s) override {
        double t=std::max(0.0,std::min<double>(radius,std::min(r.Width(),r.Height())/2));
        if (t==0) { RectangleDraw(r,s); return; }
        constexpr double pi=std::numbers::pi;
        cairo_new_sub_path(cr); cairo_arc(cr,r.right-t,r.top+t,t,-pi/2,0); cairo_arc(cr,r.right-t,r.bottom-t,t,0,pi/2);
        cairo_arc(cr,r.left+t,r.bottom-t,t,pi/2,pi); cairo_arc(cr,r.left+t,r.top+t,t,pi,3*pi/2); cairo_close_path(cr); fillStroke(s);
    }
    void GradientRectangle(PRectangle r,const std::vector<ColourStop> &stops,GradientOptions o) override {
        auto *p=cairo_pattern_create_linear(r.left,r.top,o==GradientOptions::leftToRight?r.right:r.left,o==GradientOptions::leftToRight?r.top:r.bottom);
        for (const auto &v:stops) { auto c=v.colour; cairo_pattern_add_color_stop_rgba(p,v.position,c.GetRedComponent(),c.GetGreenComponent(),c.GetBlueComponent(),c.GetAlphaComponent()); }
        cairo_set_source(cr,p); rect(r); cairo_fill(cr); cairo_pattern_destroy(p);
    }
    void DrawRGBAImage(PRectangle r,int w,int h,const unsigned char *rgba) override {
        if (w<=0 || h<=0) return;
        auto *surf=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
        auto *pixels=reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surf));
        for (int i=0;i<w*h;++i,rgba+=4) {
            unsigned a=rgba[3]; pixels[i]=(a<<24)|((rgba[0]*a/255)<<16)|((rgba[1]*a/255)<<8)|(rgba[2]*a/255);
        }
        cairo_surface_mark_dirty(surf); cairo_save(cr); rect(r); cairo_clip(cr); cairo_set_source_surface(cr,surf,r.left,r.top); cairo_paint(cr); cairo_restore(cr); cairo_surface_destroy(surf);
    }
    void Ellipse(PRectangle r,FillStroke s) override {
        if (r.Empty()) return;
        cairo_save(cr); cairo_translate(cr,r.Centre().x,r.Centre().y); cairo_scale(cr,r.Width()/2,r.Height()/2);
        cairo_arc(cr,0,0,1,0,2*std::numbers::pi); cairo_restore(cr); fillStroke(s);
    }
    void Stadium(PRectangle r,FillStroke s,Ends) override { AlphaRectangle(r,r.Height()/2,s); }
    void Copy(PRectangle r,Point from,Surface &src) override {
        cairo_save(cr); rect(r); cairo_clip(cr); cairo_set_source_surface(cr,cairo_get_target(static_cast<Canvas&>(src).cr),r.left-from.x,r.top-from.y); cairo_paint(cr); cairo_restore(cr);
    }
    // The fork uses its non-BiDi LineLayout for this UTF-8 LTR experiment.
    std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *) override { return {}; }
    void DrawTextTransparent(PRectangle r,const Font *f,XYPOSITION y,std::string_view s,ColourRGBA fg) override {
        textLayout(f,s); colour(fg); cairo_move_to(cr,r.left,y-pango_layout_get_baseline(layout)/double(PANGO_SCALE)); pango_cairo_show_layout(cr,layout);
    }
    void DrawTextNoClip(PRectangle r,const Font *f,XYPOSITION y,std::string_view s,ColourRGBA fg,ColourRGBA bg) override {
        FillRectangle(r,bg); DrawTextTransparent(r,f,y,s,fg);
    }
    void DrawTextClipped(PRectangle r,const Font *f,XYPOSITION y,std::string_view s,ColourRGBA fg,ColourRGBA bg) override {
        SetClip(r); DrawTextNoClip(r,f,y,s,fg,bg); PopClip();
    }
    void MeasureWidths(const Font *f,std::string_view s,XYPOSITION *out) override {
        if (s.empty()) return;
        textLayout(f,s);
        PangoLayoutIter *i=pango_layout_get_iter(layout);
        // Logical widths divide ligature advances between their code points.
        // The fork stores each advance at every byte of that code point.
        do {
            PangoLayoutRun *run=pango_layout_iter_get_run_readonly(i);
            if (!run) continue;
            const int begin=run->item->offset, end=begin+run->item->length;
            PangoRectangle logical; pango_layout_iter_get_run_extents(i,nullptr,&logical);
            int pen=logical.x;
            std::vector<int> widths(run->item->num_chars);
            pango_glyph_string_get_logical_widths(run->glyphs,s.data()+begin,end-begin,run->item->analysis.level,widths.data());
            int pos=begin;
            for (int advance:widths) {
                const int next=std::min<int>(end,g_utf8_next_char(s.data()+pos)-s.data());
                pen+=advance;
                std::fill(out+pos,out+next,static_cast<XYPOSITION>(pen)/PANGO_SCALE);
                pos=next;
            }
        } while(pango_layout_iter_next_run(i));
        pango_layout_iter_free(i);
        // A glyph cluster can contain a code-point boundary that is not a
        // Pango cursor stop (e.g. between e and COMBINING ACUTE ACCENT).
        // Match the strong caret of the *same* shaped line at that boundary.
        // ASCII has no such interior positions and is the hot path.
        if (std::any_of(s.begin(), s.end(), [](unsigned char c) { return c >= 0x80; })) {
            int count = 0;
            const auto *attrs = pango_layout_get_log_attrs_readonly(layout, &count);
            size_t pos = 0;
            for (int ch = 1; ch < count && pos < s.size(); ++ch) {
                const size_t next = std::min(s.size(), static_cast<size_t>(g_utf8_next_char(s.data() + pos) - s.data()));
                if (!attrs[ch].is_cursor_position) {
                    PangoRectangle strong;
                    pango_layout_get_cursor_pos(layout, next, &strong, nullptr);
                    std::fill(out + pos, out + next, static_cast<XYPOSITION>(strong.x) / PANGO_SCALE);
                }
                pos = next;
            }
        }
    }
    XYPOSITION WidthText(const Font *f,std::string_view s) override { textLayout(f,s); int w; pango_layout_get_size(layout,&w,nullptr); return XYPOSITION(w)/PANGO_SCALE; }
    void DrawTextNoClipUTF8(PRectangle r,const Font *f,XYPOSITION y,std::string_view s,ColourRGBA fg,ColourRGBA bg) override { DrawTextNoClip(r,f,y,s,fg,bg); }
    void DrawTextClippedUTF8(PRectangle r,const Font *f,XYPOSITION y,std::string_view s,ColourRGBA fg,ColourRGBA bg) override { DrawTextClipped(r,f,y,s,fg,bg); }
    void DrawTextTransparentUTF8(PRectangle r,const Font *f,XYPOSITION y,std::string_view s,ColourRGBA fg) override { DrawTextTransparent(r,f,y,s,fg); }
    void MeasureWidthsUTF8(const Font *f,std::string_view s,XYPOSITION *o) override { MeasureWidths(f,s,o); }
    XYPOSITION WidthTextUTF8(const Font *f,std::string_view s) override { return WidthText(f,s); }
    FontMetrics Metrics(const Font *f) noexcept override {
        textLayout(f,"Mg"); int h; pango_layout_get_size(layout,nullptr,&h);
        const XYPOSITION asc=std::ceil(pango_layout_get_baseline(layout)/double(PANGO_SCALE));
        const XYPOSITION height=std::ceil(h/double(PANGO_SCALE)); return {asc,height-asc,0,height};
    }
    void SetClip(PRectangle r) noexcept override { cairo_save(cr); rect(r); cairo_clip(cr); }
    void PopClip() noexcept override { cairo_restore(cr); }
    void FlushCachedState() noexcept override {}
    void FlushDrawing() noexcept override { cairo_surface_flush(cairo_get_target(cr)); }
};
}
std::shared_ptr<Font> Font::Allocate(const FontParameters &f) { return std::make_shared<PangoFont>(f); }
std::unique_ptr<Surface> Surface::Allocate(Technology) { return std::make_unique<Canvas>(); }
}
