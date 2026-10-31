// Include after key() and call testPinyin(editor) while the host window is live.
#ifndef NP4_COCOA_PINYIN_PROBE
#define NP4_COCOA_PINYIN_PROBE
#import <Carbon/Carbon.h>
static NSString *sourceID(TISInputSourceRef s) {
    return s ? (__bridge NSString *)TISGetInputSourceProperty(s,kTISPropertyInputSourceID) : nil;
}
static BOOL sourceEnabled(TISInputSourceRef s) {
    return s && [(__bridge NSNumber *)TISGetInputSourceProperty(s,kTISPropertyInputSourceIsEnabled) boolValue];
}
static NSArray *sources(NSDictionary *properties) {
    return CFBridgingRelease(TISCreateInputSourceList((__bridge CFDictionaryRef)properties,true));
}
static NSString *activeSource() {
    TISInputSourceRef current = TISCopyCurrentKeyboardInputSource();
    NSString *identifier = [sourceID(current) copy];
    if (current) CFRelease(current);
    return identifier;
}
static void testPinyin(ProbeEditor *editor) {
    NSString *target = @"com.apple.inputmethod.SCIM.ITABC";
    SCIContentView *content = editor.content;
    NSWindow *win = editor.window;
    const BOOL focused = [win makeFirstResponder:content] && win.firstResponder == content;
    pump();
    TISInputSourceRef old = TISCopyCurrentKeyboardInputSource();
    __attribute__((objc_precise_lifetime)) NSArray *modes =
        sources(@{(__bridge NSString *)kTISPropertyInputSourceID:target});
    TISInputSourceRef mode = (__bridge TISInputSourceRef)modes.firstObject;
    NSString *bundle = mode ? (__bridge NSString *)TISGetInputSourceProperty(mode,kTISPropertyBundleID) : nil;
    __attribute__((objc_precise_lifetime)) NSArray *parents = bundle ? sources(@{(__bridge NSString *)kTISPropertyBundleID:bundle,
        (__bridge NSString *)kTISPropertyInputSourceType:(__bridge NSString *)kTISTypeKeyboardInputMethodModeEnabled}) : nil;
    TISInputSourceRef parent = (__bridge TISInputSourceRef)parents.firstObject;
    const BOOL hadParent = sourceEnabled(parent), hadMode = sourceEnabled(mode);
    __block BOOL enabledParent = NO, enabledMode = NO, selected = NO;
    NSMutableDictionary *log = [@{@"before":sourceID(old) ?: @"", @"target":target,
        @"focusedBefore":@(focused), @"initiallyEnabled":@(hadMode)} mutableCopy];
    NSString *result = ^NSString *{
        if (!old || !focused) return @"no-initial-source-or-focus";
        if (!mode) return @"source-not-installed";
        if (parent && !hadParent) {
            OSStatus status = TISEnableInputSource(parent);
            log[@"parentEnableStatus"] = @(status);
            if (status != noErr) return @"parent-enable-failed";
            enabledParent = YES;
        }
        if (!sourceEnabled(mode)) {
            OSStatus status = TISEnableInputSource(mode);
            log[@"enableStatus"] = @(status);
            if (status != noErr) return @"enable-failed";
        }
        enabledMode = !hadMode && sourceEnabled(mode);
        OSStatus status = TISSelectInputSource(mode);
        log[@"selectStatus"] = @(status);
        if (status != noErr) return @"select-failed";
        selected = YES;
        for (int attempt = 0; attempt < 12; ++attempt) {
            pump();
            if ([activeSource() isEqualToString:target]) break;
        }
        log[@"afterSelect"] = activeSource() ?: @"";
        if (![log[@"afterSelect"] isEqual:target]) return @"source-not-active";
        [editor setString:@""];
        [editor message:SCI_EMPTYUNDOBUFFER];
        NSMutableArray *steps = [NSMutableArray array];
        const unsigned short codes[] = {45,34,4,0,31};
        NSString *letters = @"nihao";
        BOOL sawMarked = NO;
        for (NSUInteger i = 0; i < letters.length; ++i) {
            key(win,codes[i],[letters substringWithRange:NSMakeRange(i,1)],0);
            sawMarked |= content.hasMarkedText;
            [steps addObject:@{@"marked":@(content.hasMarkedText),
                @"range":NSStringFromRange(content.markedRange), @"text":editor.string ?: @""}];
        }
        key(win,49,@" ",0);
        NSString *text = editor.string ?: @"";
        BOOL chinese = [text rangeOfCharacterFromSet:[NSCharacterSet
            characterSetWithRange:NSMakeRange(0x4e00,0x9fff-0x4e00+1)]].location != NSNotFound;
        BOOL ended = !content.hasMarkedText, focusAfter = win.firstResponder == content;
        log[@"steps"] = steps; log[@"committed"] = text; log[@"hasCJK"] = @(chinese);
        log[@"ended"] = @(ended); log[@"focusedAfter"] = @(focusAfter);
        log[@"afterKeys"] = activeSource() ?: @"";
        [editor message:SCI_UNDO];
        BOOL restored = [editor.string isEqualToString:@""];
        log[@"undoText"] = editor.string ?: @"";
        return sawMarked && chinese && ended && focusAfter && restored &&
            [log[@"afterKeys"] isEqual:target] ? @"pass" : @"source-active-behaviour-incomplete";
    }();
    log[@"result"] = result;
    if ((selected || enabledParent || enabledMode) && old) {
        log[@"restoreStatus"] = @(TISSelectInputSource(old));
        pump();
    }
    if (enabledMode) log[@"disableModeStatus"] = @(TISDisableInputSource(mode));
    if (enabledParent) log[@"disableParentStatus"] = @(TISDisableInputSource(parent));
    log[@"restoredSource"] = activeSource() ?: @"";
    if (old) CFRelease(old);
    NSData *json = [NSJSONSerialization dataWithJSONObject:log options:0 error:NULL];
    std::printf("PINYIN %s\n", [[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding] UTF8String]);
    fflush(stdout);
}
#endif
