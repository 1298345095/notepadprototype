#include "fork.hpp"
#import <AppKit/AppKit.h>
#include <cstdio>
#include <cmath>

using Doc = Scintilla::Internal::Document;
using LexIface = Scintilla::Internal::LexInterface;
using Pos = Sci::Position;
extern const Lexilla::LexerModule lmJSON;

class JsonSyntax final : public LexIface {
public:
    explicit JsonSyntax(Doc *doc) : LexIface(doc) {
        instance.reset(lmJSON.Create());
        instance->WordListSet(0, 0, "Infinity NaN false null true");
    }
};

static const std::string sample =
    "{\n  name: 'Notepad4 中文😀',\n  platform: 'AppKit',\n  native: true,\n  tabSize: 4\n}\n";

static std::string bytes(const Doc &doc, Pos first, Pos last) {
    std::string data(static_cast<size_t>(last - first), '\0');
    if (!data.empty()) doc.GetCharRange(data.data(), first, last - first);
    return data;
}

static NSColor *colour(int style) {
    switch (style) {
        case SCE_JSON_PROPERTYNAME: return [NSColor colorWithRed:0.11 green:0.23 blue:0.65 alpha:1];
        case SCE_JSON_STRING_SQ: case SCE_JSON_STRING_DQ:
            return [NSColor colorWithRed:0.05 green:0.43 blue:0.27 alpha:1];
        case SCE_JSON_KEYWORD: return [NSColor colorWithRed:0.54 green:0.18 blue:0.52 alpha:1];
        default: return NSColor.blackColor;
    }
}

@interface DocumentView : NSView {
    std::unique_ptr<Doc> doc;
    Pos caret;
    NSFont *font;
}
- (BOOL)verifyWithImage:(NSString *)path;
@end

@implementation DocumentView
- (instancetype)initWithFrame:(NSRect)r {
    self = [super initWithFrame:r];
    if (self) {
        doc = std::make_unique<Doc>(Scintilla::DocumentOption::Default);
        doc->SetLexInterface(std::make_unique<JsonSyntax>(doc.get()));
        doc->SetUndoCollection(false);
        doc->InsertString(0, sample);
        doc->SetUndoCollection(true);
        doc->SetSavePoint();
        caret = doc->Length();
        font = [NSFont monospacedSystemFontOfSize:15 weight:NSFontWeightRegular];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty {
    [NSColor.whiteColor setFill];
    NSRectFill(dirty);
    doc->EnsureStyledTo(doc->Length());
    const CGFloat lineHeight = std::ceil(font.ascender - font.descender + font.leading + 3);
    for (Sci::Line line = 0; line < doc->LinesTotal(); ++line) {
        const CGFloat y = 10 + line * lineHeight;
        if (y >= NSMaxY(dirty)) break;
        if (y + lineHeight < NSMinY(dirty)) continue;
        CGFloat x = 12;
        const Pos lineEnd = doc->LineEnd(line);
        for (Pos from = doc->LineStart(line); from < lineEnd;) {
            const int style = doc->StyleAt(from);
            Pos to = from + 1;
            while (to < lineEnd && doc->StyleAt(to) == style) ++to;
            const auto run = bytes(*doc, from, to);
            NSString *text = [[NSString alloc] initWithBytes:run.data() length:run.size()
                                                    encoding:NSUTF8StringEncoding];
            NSDictionary *attrs = @{NSFontAttributeName: font, NSForegroundColorAttributeName: colour(style)};
            [text drawAtPoint:NSMakePoint(x, y) withAttributes:attrs];
            x += [text sizeWithAttributes:attrs].width;
            from = to;
        }
        if (self.window.firstResponder == self && caret == lineEnd) {
            [NSColor.darkGrayColor setFill];
            NSRectFill(NSMakeRect(x, y, 1, lineHeight));
        }
    }
}

- (void)insertText:(id)value {
    NSString *text = [value isKindOfClass:NSAttributedString.class] ? [value string] : value;
    NSData *utf8 = [text dataUsingEncoding:NSUTF8StringEncoding];
    if (utf8.length) {
        caret += doc->InsertString(caret, std::string_view(static_cast<const char *>(utf8.bytes), utf8.length));
        self.needsDisplay = YES;
    }
}
- (void)deleteBackward:(id)sender {
    if (caret > 0) {
        Pos previous = doc->NextPosition(caret, -1);
        doc->DeleteChars(previous, caret - previous);
        caret = previous;
        self.needsDisplay = YES;
    }
}
- (void)insertNewline:(id)sender { [self insertText:@"\n"]; }
- (void)undo:(id)sender {
    if (doc->CanUndo()) {
        caret = doc->Undo();
        self.needsDisplay = YES;
    }
}
- (void)keyDown:(NSEvent *)event {
    if ((event.modifierFlags & NSEventModifierFlagCommand) && [event.charactersIgnoringModifiers isEqualToString:@"z"])
        [self undo:nil];
    else [self interpretKeyEvents:@[event]];
}

- (BOOL)verifyWithImage:(NSString *)path {
    if (self.window.firstResponder != self) return NO;
    doc->EnsureStyledTo(doc->Length());
    Sci_Position width = 0;
    if (doc->StyleAt(sample.find("name")) != SCE_JSON_PROPERTYNAME ||
        doc->StyleAt(sample.find("true")) != SCE_JSON_KEYWORD ||
        doc->GetCharacterAndWidth(sample.find("😀"), &width) != 0x1f600 || width != 4) return NO;
    [self insertText:@"测试😀"];
    if (bytes(*doc, 0, doc->Length()) != sample + "测试😀") return NO;
    [self undo:nil];
    if (bytes(*doc, 0, doc->Length()) != sample) return NO;

    NSBitmapImageRep *bitmap = [self bitmapImageRepForCachingDisplayInRect:self.bounds];
    if (!bitmap) return NO;
    [self cacheDisplayInRect:self.bounds toBitmapImageRep:bitmap];
    // Require actual non-neutral ink on the AppKit surface, independent of DPI.
    NSUInteger ink = 0;
    for (NSInteger y = 0; y < bitmap.pixelsHigh && ink < 20; y += 2) {
        for (NSInteger x = 0; x < bitmap.pixelsWide; x += 2) {
            NSColor *c = [[bitmap colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
            if (c && c.blueComponent - c.redComponent > 0.15) ++ink;
        }
    }
    NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return ink >= 20 && [png writeToFile:path atomically:YES];
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)application { return YES; }
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        Scintilla::Internal::CharClassify::InitUnicodeData();
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [AppDelegate new];
        app.delegate = delegate;
        NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 600, 260)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        win.releasedWhenClosed = NO;
        win.title = @"Notepad4 · AppKit document probe";
        DocumentView *view = [[DocumentView alloc] initWithFrame:win.contentView.bounds];
        win.contentView = view;
        [app finishLaunching];
        [win makeKeyAndOrderFront:nil];
        [win makeFirstResponder:view];
        if (argc == 3 && std::string_view(argv[1]) == "--self-test") {
            const BOOL ok = [view verifyWithImage:[NSString stringWithUTF8String:argv[2]]];
            if (ok) puts("PASS arm64 AppKit NSWindow/NSView, fork JSON5, 4-byte scalar, insert and undo, actual coloured pixels");
            return ok ? 0 : 1;
        }
        [app run];
        return 0;
    }
}
