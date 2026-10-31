#import <Cocoa/Cocoa.h>
#import "ScintillaView.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#if defined(PROBE_FLTK)
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>
#else
#include <wx/wx.h>
#include <wx/nativewin.h>
#endif

static unsigned ordinary, arrow, command, clicks;
@interface TraceContent : SCIContentView
@end
@implementation TraceContent
- (void)keyDown:(NSEvent *)event {
    if (event.modifierFlags & NSEventModifierFlagCommand) ++command;
    else if (event.keyCode == 123) ++arrow;
    else ++ordinary;
    [super keyDown:event];
}
- (void)mouseDown:(NSEvent *)event { ++clicks; [super mouseDown:event]; }
@end
@interface ProbeEditor : ScintillaView
@end
@implementation ProbeEditor
+ (Class)contentViewClass { return TraceContent.class; }
@end

static void pump() {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:0.15];
    while (deadline.timeIntervalSinceNow > 0) {
#if defined(PROBE_FLTK)
        Fl::wait(0.01);
#else
        wxTheApp->Yield(true);
        [NSThread sleepForTimeInterval:0.005];
#endif
    }
}
static void key(NSWindow *window, unsigned short code, NSString *ch, NSEventModifierFlags flags) {
    for (auto type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
        NSEvent *event = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags
            timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber
            context:nil characters:ch charactersIgnoringModifiers:ch isARepeat:NO keyCode:code];
        [NSApp postEvent:event atStart:NO];
    }
    pump();
}
#include "pinyin.inc"

static int test(ProbeEditor *editor) {
    SCIContentView *content = [editor content];
    NSWindow *win = editor.window;
    [NSApp activateIgnoringOtherApps:YES];
    [win makeKeyAndOrderFront:nil];
    pump();
    BOOL focused = [win makeFirstResponder:content] && win.firstResponder == content;
#if defined(PROBE_FLTK)
    id monitor = nil;
    if (std::getenv("NP4_ROUTE_ARROWS")) {
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *e) {
            if (e.window == win && win.firstResponder == content && e.keyCode >= 123 && e.keyCode <= 126 &&
                !(e.modifierFlags & (NSEventModifierFlagControl | NSEventModifierFlagCommand |
                                     NSEventModifierFlagOption | NSEventModifierFlagShift))) {
                [content keyDown:e];
                return nil;
            }
            return e;
        }];
    }
#endif
    [editor message:SCI_SETCODEPAGE wParam:SC_CP_UTF8];
    [editor setString:@"A中😀B"];
    [editor message:SCI_SETSEL wParam:1 lParam:8];
    BOOL selection = NSEqualRanges(content.selectedRange, NSMakeRange(1, 3));
    NSRange actual = NSMakeRange(NSNotFound, 0);
    NSRect r = [content firstRectForCharacterRange:NSMakeRange(1, 1) actualRange:&actual];
    BOOL rect = r.size.width > 0 && r.size.height > 0 && NSIntersectsRect(win.frame, r);
    NSPoint screen = NSMakePoint(NSMinX(r) + 0.25 * NSWidth(r), NSMidY(r));
    NSUInteger index = [content characterIndexForPoint:screen];
    const NSRange noReplace = NSMakeRange(NSNotFound,0);
    id<NSTextInputClient> responder = (id)win.firstResponder;
    if (![(NSObject *)responder conformsToProtocol:@protocol(NSTextInputClient)]) return 2;
    [responder setMarkedText:@"zhong" selectedRange:NSMakeRange(5,0) replacementRange:noReplace];
    BOOL marked = responder.hasMarkedText;
    [responder insertText:@"文" replacementRange:noReplace];
    BOOL committed = !responder.hasMarkedText && [[editor string] isEqualToString:@"A文B"];
    // Click via the host's event loop. Keep it inside the text viewport.
    NSPoint inWindow = [win convertPointFromScreen:screen];
    for (auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
        [NSApp postEvent:[NSEvent mouseEventWithType:type location:inWindow modifierFlags:0
            timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:win.windowNumber
            context:nil eventNumber:1 clickCount:1 pressure:1.0] atStart:NO];
    }
    pump();
    [editor setString:@""];
    key(win, 0, @"a", 0);
    BOOL typed = [[editor string] isEqualToString:@"a"];
    key(win, 123, @"\uF702", NSEventModifierFlagNumericPad | NSEventModifierFlagFunction);
    BOOL moved = [editor message:SCI_GETCURRENTPOS] == 0;
    key(win, 0, @"a", NSEventModifierFlagCommand);
    // Both the menu responder chain and direct keyDown can deliver Select All.
    BOOL selectedAll = [[editor selectedString] isEqualToString:@"a"];
    BOOL keyWindow = (NSApp.keyWindow == win);
    [editor setString:@"head\ninside\nafter\n"];
    [editor message:SCI_SETFOLDLEVEL wParam:0 lParam:SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG];
    [editor message:SCI_SETFOLDLEVEL wParam:1 lParam:SC_FOLDLEVELBASE + 1];
    [editor message:SCI_SETFOLDLEVEL wParam:2 lParam:SC_FOLDLEVELBASE];
    [editor message:SCI_FOLDLINE wParam:0 lParam:SC_FOLDACTION_CONTRACT];
    BOOL folded = ![editor message:SCI_GETLINEVISIBLE wParam:1] &&
        [editor message:SCI_DOCLINEFROMVISIBLE wParam:1] == 2;
    [editor setString:@"al"];
    [editor message:SCI_GOTOPOS wParam:2];
    [editor message:SCI_AUTOCSHOW wParam:2 lParam:reinterpret_cast<sptr_t>("alpha alpine")];
    pump();
    BOOL popup = [editor message:SCI_AUTOCACTIVE];
    [editor message:SCI_AUTOCCOMPLETE];
    BOOL completed = [[editor string] isEqualToString:@"alpha"];
    std::printf("ROUTE keyWindow=%d focus=%d utf16Selection=%d rect=%d point=%lu marked=%d "
        "commit=%d mouseDown=%u ordinaryKeyDown=%u typed=%d leftKeyDown=%u moved=%d "
        "cmdKeyDown=%u cmdASelectedAll=%d folded=%d popup=%d completed=%d\n",
        keyWindow, focused, selection, rect, (unsigned long)index, marked, committed,
        clicks, ordinary, typed, arrow, moved, command, selectedAll, folded, popup, completed);
    fflush(stdout);
    testPinyin(editor);
#if defined(PROBE_FLTK)
    if (monitor) [NSEvent removeMonitor:monitor];
    if (std::getenv("NP4_ROUTE_ARROWS") && (!moved || arrow != 1)) return 4;
#endif
    // Keep shortcuts diagnostic so one unsuccessful key never hides other results.
    return !(focused && selection && rect && index == 1 && marked && committed && clicks && typed
        && folded && popup && completed);
}
#if defined(PROBE_FLTK)
int main() {
    @autoreleasepool {
        Fl_Window shell(500, 280, "FLTK host");
        shell.end(); shell.show();
        NSWindow *native = (NSWindow *)fl_mac_xid(&shell);
        NSView *parent = native.contentView;
        ProbeEditor *editor = [[ProbeEditor alloc] initWithFrame:parent.bounds];
        editor.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [parent addSubview:editor];
        int status = test(editor);
        [editor removeFromSuperview];
        shell.hide();
        return status;
    }
}
#else
class App : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(App);
int main(int argc, char **argv) {
    @autoreleasepool {
        if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 3;
        auto *frame = new wxFrame(nullptr, wxID_ANY, "wx host", wxDefaultPosition, wxSize(500,280));
        ProbeEditor *editor = [[ProbeEditor alloc] initWithFrame:NSMakeRect(0,0,480,250)];
        auto *child = new wxNativeWindow(frame, wxID_ANY, editor);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(child, 1, wxEXPAND); frame->SetSizer(sizer); frame->Show(); frame->Layout();
        pump();
        int status = test(editor);
        frame->Destroy(); wxTheApp->OnExit(); wxEntryCleanup();
        return status;
    }
}
#endif
