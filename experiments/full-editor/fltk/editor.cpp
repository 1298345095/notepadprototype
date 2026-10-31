#include "fork_editor.hpp"
#include "CaseConvert.h"
#include "study.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <pango/pangocairo.h>
#include <chrono>

using namespace Scintilla;
using namespace Scintilla::Internal;
namespace {
unsigned eventTime() {
    return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
KeyMod modifiers() {
    int mask=0;
    if(Fl::event_shift()) mask|=static_cast<int>(KeyMod::Shift);
    if(Fl::event_ctrl()) mask|=static_cast<int>(KeyMod::Ctrl);
    if(Fl::event_alt()) mask|=static_cast<int>(KeyMod::Alt);
    return static_cast<KeyMod>(mask);
}
Keys editorKey(int k) {
    switch(k) {
        case FL_Left:return Keys::Left; case FL_Right:return Keys::Right;
        case FL_Up:return Keys::Up; case FL_Down:return Keys::Down;
        case FL_Home:return Keys::Home; case FL_End:return Keys::End;
        case FL_Page_Up:return Keys::Prior; case FL_Page_Down:return Keys::Next;
        case FL_Insert:return Keys::Insert; case FL_Delete:return Keys::Delete;
        case FL_BackSpace:return Keys::Back; case FL_Escape:return Keys::Escape;
        case FL_Enter:case FL_KP_Enter:return Keys::Return; case FL_Tab:return Keys::Tab;
        default:return static_cast<Keys>(k>='a' && k<='z'? k-'a'+'A':k);
    }
}
class ForkWidget final : public Fl_Widget, public ScintillaBase {
    bool captured=false, idling=false;
    struct Ticker { ForkWidget *self=nullptr; TickReason reason{}; double period=0; };
    std::array<Ticker,5> tickers{};
    static void timer(void *d) { auto *t=static_cast<Ticker*>(d); t->self->TickFor(t->reason); if(t->period) Fl::repeat_timeout(t->period,timer,t); }
    static void runIdle(void *p) { auto *e=static_cast<ForkWidget*>(p); if(!e->Idle()) e->SetIdle(false); }
    static void qa(void *p) { auto *e=static_cast<ForkWidget*>(p); np4qa::tick(e->x(),e->y()); Fl::repeat_timeout(.03,qa,p); }
    Point mouse() const { return Point::FromInts(Fl::event_x()-x(),Fl::event_y()-y()); }
    void insertUtf8(std::string_view s,CharacterSource source) {
        for(size_t i=0;i<s.size();) {
            size_t width=std::min<size_t>(s.size()-i,g_utf8_next_char(s.data()+i)-(s.data()+i));
            InsertCharacter(s.substr(i,width),source); i+=width;
        }
    }
    bool batch(wl_surface *surface,const Fl_Wayland_Text_Edit &e) {
        if(Fl::focus()!=this || !window() || surface!=fl_wl_surface(fl_wl_xid(window()))) return false;
        bool composing=pdoc->TentativeActive();
        if(!e.has_deletion && !e.has_preedit && !e.has_commit && !composing) return false;
        if(composing) pdoc->TentativeUndo();
        if(e.has_deletion) {
            auto pos=CurrentPosition();
            if(e.delete_before<=static_cast<size_t>(pos) && e.delete_after<=static_cast<size_t>(pdoc->Length()-pos))
                pdoc->DeleteChars(pos-e.delete_before,static_cast<Sci::Position>(e.delete_before)+e.delete_after);
        }
        if(e.commit && *e.commit) {
            UndoGroup group(pdoc);
            insertUtf8(e.commit,composing?CharacterSource::ImeResult:CharacterSource::DirectInput);
        }
        if(e.preedit && *e.preedit) {
            if(!composing) ClearBeforeTentativeStart();
            pdoc->TentativeStart();
            std::string_view preedit(e.preedit);
            insertUtf8(preedit,CharacterSource::TentativeInput);
            DrawImeIndicator(IndicatorInput,preedit.size());
            if(e.cursor_begin>=0 && static_cast<size_t>(e.cursor_begin)<=preedit.size())
                MoveImeCarets(static_cast<Sci::Position>(e.cursor_begin)-static_cast<Sci::Position>(preedit.size()));
        }
        EnsureCaretVisible(); redraw(); return true;
    }
public:
    explicit ForkWidget(int w,int h):Fl_Widget(0,0,w,h) {
        wMain=static_cast<Fl_Widget*>(this);
    }
    void start(const char *file) {
        np4qa::attach([this](unsigned m,uintptr_t wp,intptr_t lp) { return WndProc(static_cast<Message>(m),wp,lp); },file);
        fl_wl_set_edit_handler([](wl_surface *s,const Fl_Wayland_Text_Edit &e,void *p){return static_cast<ForkWidget*>(p)->batch(s,e);},this);
        Fl::add_timeout(.03,qa,this); take_focus(); ChangeSize(); redraw();
    }
    ~ForkWidget() override {
        fl_wl_set_edit_handler(nullptr,nullptr); Fl::remove_timeout(qa,this);
        for(auto &t:tickers) Fl::remove_timeout(timer,&t);
        Finalise(); wMain=nullptr;
    }
    void resize(int x,int y,int w,int h) override { Fl_Widget::resize(x,y,w,h); ChangeSize(); }
    void draw() override {
        auto *cr=fl_wl_gc(); if(!cr) return;
        cairo_save(cr); cairo_translate(cr,x(),y());
        auto surface=Surface::Allocate(Technology::Default); surface->Init(cr,wMain.GetID());
        paintState=PaintState::painting; paintingAllText=true; rcPaint=GetClientRectangle();
        Paint(surface.get(),rcPaint); paintState=PaintState::notPainting;
        cairo_restore(cr);
        auto caret=PointMainCaret(); fl_set_spot(FL_COURIER,16,x()+int(caret.x),y()+int(caret.y)+vs.lineHeight,0,vs.lineHeight,window());
    }
    int handle(int evt) override {
        switch(evt) {
            case FL_FOCUS: SetFocusState(true); return 1;
            case FL_UNFOCUS: SetFocusState(false); return 1;
            case FL_PUSH: if(Fl::event_button()!=FL_LEFT_MOUSE) return 0;
                take_focus(); ButtonDownWithModifiers(mouse(),eventTime(),modifiers()); return 1;
            case FL_DRAG: ButtonMoveWithModifiers(mouse(),eventTime(),modifiers()); return 1;
            case FL_RELEASE: ButtonUpWithModifiers(mouse(),eventTime(),modifiers()); return 1;
            case FL_MOUSEWHEEL: ScrollTo(topLine+Fl::event_dy()*3); return 1;
            case FL_PASTE: InsertPaste(std::string_view(Fl::event_text(),Fl::event_length())); return 1;
            case FL_KEYDOWN: {
                if(Fl::event_ctrl() && Fl::event_key()==' ') {np4qa::complete();return 1;}
                bool consumed=false;
                KeyDownWithModifiers(editorKey(Fl::event_key()),modifiers(),&consumed);
                if(consumed) return 1;
                if(!Fl::event_ctrl() && !Fl::event_alt() && Fl::event_length()>0) {
                    insertUtf8({Fl::event_text(),static_cast<size_t>(Fl::event_length())},CharacterSource::DirectInput); return 1;
                }
                return 0;
            }
            default:return Fl_Widget::handle(evt);
        }
    }
    void NotifyCaretMove() const noexcept override {}
    void UpdateSystemCaret() override {}
    void SetHorizontalScrollPos() override {}
    bool ModifyScrollBars(Sci::Line,Sci::Line) override { return false; }
    void Copy(bool) const override { SelectionText text; CopySelectionRange(text); CopyToClipboard(text); }
    void CopyToClipboard(const SelectionText &s) const override { Fl::copy(s.Data(),s.Length(),1); }
    void Paste(bool) override { Fl::paste(*this,1); }
    void NotifyChange() const noexcept override {}
    void NotifyParent(NotificationData &s) const noexcept override { np4qa::notify(static_cast<unsigned>(s.nmhdr.code),s.ch,static_cast<int>(s.characterSource),s.listType); }
    std::string CaseMapString(const std::string &s,CaseMapping c) const override { return c==CaseMapping::same?s:CaseConvertString(s,c==CaseMapping::upper?CaseConversion::upper:CaseConversion::lower); }
    void StartDrag() override {}
    bool FineTickerRunning(TickReason r) const noexcept override { return tickers[static_cast<size_t>(r)].period>0; }
    void FineTickerStart(TickReason r,int ms,int) noexcept override { auto &t=tickers[static_cast<size_t>(r)]; FineTickerCancel(r); t={this,r,ms/1000.0}; Fl::add_timeout(t.period,timer,&t); }
    void FineTickerCancel(TickReason r) noexcept override { auto &t=tickers[static_cast<size_t>(r)]; Fl::remove_timeout(timer,&t); t.period=0; }
    bool SetIdle(bool on) noexcept override { if(on!=idling){idling=on; if(on)Fl::add_idle(runIdle,this);else Fl::remove_idle(runIdle,this);}return true; }
    void SetMouseCapture(bool on) noexcept override { captured=on; }
    bool HaveMouseCapture() const noexcept override { return captured; }
    void UpdateBaseElements() noexcept override {}
    bool ValidCodePage(int cp) const noexcept override { return cp==SC_CP_UTF8; }
    std::string UTF8FromEncoded(std::string_view s) const override { return std::string(s); }
    std::string EncodedFromUTF8(std::string_view s) const override { return std::string(s); }
    sptr_t DefWndProc(Message,uptr_t,sptr_t) override { return 0; }
    void CreateCallTipWindow(PRectangle) noexcept override {}
};
}
int main(int argc,char **argv) {
    CharClassify::InitUnicodeData();
    Fl_Window window(680,480,"NP4 FLTK Study");
    ForkWidget editor(window.w(),window.h()); window.end(); window.resizable(editor); window.show();
    if(!fl_wl_display() || !fl_wl_xid(&window) || !fl_wl_surface(fl_wl_xid(&window))) {std::fputs("native Wayland is required\n",stderr);return 2;}
    editor.start(argc>1?argv[1]:nullptr); return Fl::run();
}
