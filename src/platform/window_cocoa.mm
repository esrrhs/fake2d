#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <dispatch/dispatch.h>

namespace {

struct CocoaImeRect {
    int x = 0;
    int y = 0;
    int h = 0;
    bool valid = false;
};

static const void *kImeRectKey = &kImeRectKey;
using FirstRectImp = NSRect (*)(id, SEL, NSRange, NSRangePointer);
static FirstRectImp gOrigFirstRect = nullptr;

NSRect Fake2dFirstRectForCharRange(id self, SEL _cmd, NSRange range, NSRangePointer actualRange) {
    NSView *view = static_cast<NSView *>(self);
    NSValue *val = static_cast<NSValue *>(objc_getAssociatedObject(view, kImeRectKey));
    if (val != nil && [view window] != nil) {
        CocoaImeRect r{};
        [val getValue:&r];
        if (r.valid && r.h > 0) {
            const NSRect bounds = [view bounds];
            const CGFloat flippedY = [view isFlipped]
                                         ? static_cast<CGFloat>(r.y)
                                         : (bounds.size.height - static_cast<CGFloat>(r.y + r.h));
            const NSRect localRect = NSMakeRect(static_cast<CGFloat>(r.x), flippedY, 2.0,
                                                static_cast<CGFloat>(r.h));
            const NSRect winRect = [view convertRect:localRect toView:nil];
            const NSRect screenRect = [[view window] convertRectToScreen:winRect];
            if (actualRange) {
                *actualRange = NSMakeRange(0, 0);
            }
            return screenRect;
        }
    }
    if (gOrigFirstRect) {
        return gOrigFirstRect(self, _cmd, range, actualRange);
    }
    return NSZeroRect;
}

void EnsureImeSwizzleInstalled(NSView *view) {
    if (view == nil) {
        return;
    }
    static dispatch_once_t once;
    static Class installed_cls = Nil;
    Class cls = [view class];
    dispatch_once(&once, ^{
        SEL sel = @selector(firstRectForCharacterRange:actualRange:);
        Method m = class_getInstanceMethod(cls, sel);
        if (m != nullptr) {
            gOrigFirstRect = reinterpret_cast<FirstRectImp>(
                method_setImplementation(m, reinterpret_cast<IMP>(Fake2dFirstRectForCharRange)));
            installed_cls = cls;
        }
    });
    (void)installed_cls;
}

} // namespace

namespace fake2d::platform::detail {

void SetCocoaImeCursorRect(GLFWwindow *handle, int window_x, int window_y, int line_height) {
    if (!handle) {
        return;
    }
    NSWindow *nswin = glfwGetCocoaWindow(handle);
    if (nswin == nil) {
        return;
    }
    NSView *view = [nswin contentView];
    if (view == nil) {
        return;
    }
    EnsureImeSwizzleInstalled(view);
    CocoaImeRect rect{window_x, window_y, line_height, line_height > 0};
    NSValue *val = [NSValue valueWithBytes:&rect objCType:@encode(CocoaImeRect)];
    objc_setAssociatedObject(view, kImeRectKey, val, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

void ClearCocoaImeCursorRect(GLFWwindow *handle) {
    if (!handle) {
        return;
    }
    NSWindow *nswin = glfwGetCocoaWindow(handle);
    if (nswin == nil) {
        return;
    }
    NSView *view = [nswin contentView];
    if (view != nil) {
        objc_setAssociatedObject(view, kImeRectKey, nil, OBJC_ASSOCIATION_ASSIGN);
    }
}

} // namespace fake2d::platform::detail

#endif
