#ifdef VOSC_WEBGPU_M1

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void* voscM1MetalLayer(void* cocoaWindow) {
    NSWindow* window = (__bridge NSWindow*)cocoaWindow;
    NSView* view = window.contentView;
    auto* layer = [CAMetalLayer layer];
    layer.contentsScale = window.backingScaleFactor;
    view.wantsLayer = YES;
    view.layer = layer;
    return (__bridge void*)layer;
}

#endif
