#include "fork_editor.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Hold_Browser.H>

namespace Scintilla::Internal {
namespace {
Fl_Widget *widget(WindowID id) { return static_cast<Fl_Widget *>(id); }
class Candidates final : public ListBox {
    std::vector<std::string> words;
    int rowHeight=18, rows=7, charWidth=9;
    IListBoxDelegate *delegate=nullptr;
    Fl_Hold_Browser *browser() const { return static_cast<Fl_Hold_Browser *>(wid); }
public:
    void SetFont(std::shared_ptr<Font>) noexcept override { if (wid) browser()->textsize(std::max(10,rowHeight-3)); }
    void Create(Window &parent,int,Point,int height,int,Technology) noexcept override {
        rowHeight=height;
        auto *editor=widget(parent.GetID());
        auto *list=new Fl_Hold_Browser(editor->x(),editor->y(),160,rowHeight*rows);
        wid=list;
        editor->window()->add(list);
        list->clear_visible_focus(); list->textfont(FL_COURIER); list->format_char(0); list->hide();
        list->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE_ALWAYS);
        list->callback([](Fl_Widget *,void *data) {
            auto &self=*static_cast<Candidates *>(data);
            if (self.delegate) {
                ListBoxEvent ev(Fl::event_clicks()?ListBoxEvent::EventType::doubleClick:ListBoxEvent::EventType::selectionChange);
                self.delegate->ListNotify(&ev);
            }
        },this);
    }
    void SetAverageCharWidth(int w,double) noexcept override { charWidth=w; }
    void SetVisibleRows(int n) noexcept override { rows=std::max(1,n); }
    int GetVisibleRows() const noexcept override { return rows; }
    PRectangle GetDesiredRect() override {
        size_t longest=1; for (const auto &v:words) longest=std::max(longest,v.size());
        return PRectangle::FromInts(0,0,std::min(480,static_cast<int>(longest)*charWidth+26),std::min(rows,std::max(1,Length()))*rowHeight+8);
    }
    int CaretFromEdge() const noexcept override { return 4; }
    void Clear() noexcept override { words.clear(); if(wid) browser()->clear(); }
    int Length() const noexcept override { return static_cast<int>(words.size()); }
    void Select(int n) override { if (wid) { browser()->value(n+1); if(n>=0) browser()->make_visible(n+1); } }
    int GetSelection() const noexcept override { return wid? browser()->value()-1:-1; }
    std::string GetValue(int n) const override { return n>=0 && n<Length()?words[n]:""; }
    void RegisterImage(int,const char*) override {}
    void RegisterRGBAImage(int,int,int,const unsigned char*) override {}
    void ClearRegisteredImages() noexcept override {}
    void SetDelegate(IListBoxDelegate *d) noexcept override { delegate=d; }
    void SetList(const char *s,char sep,char typeSep) override {
        Clear();
        std::string_view all(s);
        while (!all.empty()) {
            auto end=all.find(sep); auto entry=all.substr(0,end);
            words.emplace_back(entry.substr(0,entry.find(typeSep))); browser()->add(words.back().c_str());
            if (end==all.npos) break;
            all.remove_prefix(end+1);
        }
    }
    void SetOptions(const ListOptions &) noexcept override {}
};
}
std::unique_ptr<ListBox> ListBox::Allocate() { return std::make_unique<Candidates>(); }
void Window::Destroy() noexcept { if(wid) { auto *w=widget(wid); w->hide(); if(w->parent()) w->parent()->remove(w); Fl::delete_widget(w); wid=nullptr; } }
PRectangle Window::GetPosition() const noexcept { if(!wid) return PRectangle(); auto *w=widget(wid); return PRectangle::FromInts(w->x(),w->y(),w->x()+w->w(),w->y()+w->h()); }
void Window::SetPosition(PRectangle r) noexcept { if(wid) widget(wid)->resize(r.left,r.top,r.Width(),r.Height()); }
void Window::SetPositionRelative(PRectangle r,const Window *to) noexcept { if(to&&to->Created()) { auto p=to->GetPosition(); r.Move(p.left,p.top); } SetPosition(r); }
PRectangle Window::GetClientPosition() const noexcept { return wid?PRectangle::FromInts(0,0,widget(wid)->w(),widget(wid)->h()):PRectangle(); }
void Window::Show(bool on) const noexcept { if(wid) { if(on) widget(wid)->show(); else widget(wid)->hide(); } }
void Window::InvalidateAll() noexcept { if(wid) widget(wid)->redraw(); }
void Window::InvalidateRectangle(PRectangle) noexcept { InvalidateAll(); }
void Window::SetCursor(Cursor c) const noexcept { if(wid && widget(wid)->window()) widget(wid)->window()->cursor(c==Cursor::text?FL_CURSOR_INSERT:FL_CURSOR_DEFAULT); }
PRectangle Window::GetMonitorRect(Point) const noexcept { return GetClientPosition(); }
void Menu::CreatePopUp() noexcept {}
void Menu::Destroy() noexcept {}
void Menu::Show(Point,const Window &) noexcept {}
namespace Platform {
ColourRGBA Chrome() noexcept { return ColourRGBA(235,235,235); }
ColourRGBA ChromeHighlight() noexcept { return white; }
const char *DefaultFont() noexcept { return "DejaVu Sans Mono"; }
int DefaultFontSize() noexcept { return 12; }
unsigned int DoubleClickTime() noexcept { return 350; }
}
}
