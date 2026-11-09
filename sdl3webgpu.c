/**
 * This is an extension of SDL3 for WebGPU, abstracting away the details of
 * OS-specific operations.
 * 
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://eliemichel.github.io/LearnWebGPU
 * 
 * Most of this code comes from the wgpu-native triangle example:
 *   https://github.com/gfx-rs/wgpu-native/blob/master/examples/triangle/main.c
 * 
 * MIT License
 * Copyright (c) 2022-2024 Elie Michel and the wgpu-native authors
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "sdl3webgpu.h"

#include <webgpu/webgpu.h>

#if defined(SDL_PLATFORM_MACOS)
#  include <Cocoa/Cocoa.h>
#  include <Foundation/Foundation.h>
#  include <QuartzCore/CAMetalLayer.h>
#  include <QuartzCore/CATransaction.h>
#  include <objc/runtime.h>
#elif defined(SDL_PLATFORM_IOS)
#  include <UIKit/UIKit.h>
#  include <Foundation/Foundation.h>
#  include <QuartzCore/CAMetalLayer.h>
#  include <Metal/Metal.h>
#elif defined(SDL_PLATFORM_WIN32)
#  include <windows.h>
#endif

#include <SDL3/SDL.h>

WGPUSurface SDL_GetWGPUSurface(WGPUInstance instance, SDL_Window* window) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);

#if defined(SDL_PLATFORM_MACOS)
    {
        id metal_layer = NULL;
        NSView *content_view = NULL;

        // Try to get the NSWindow first (standalone window mode)
        NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
        if (ns_window) {
            content_view = ns_window.contentView;
        } else {
            // Embedded mode: no NSWindow, get the content view directly
            content_view = (__bridge NSView *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_CONTENTVIEW_POINTER, NULL);
        }

        if (!content_view) return NULL;

        [content_view setWantsLayer : YES];
        metal_layer = [CAMetalLayer layer];
        // Note: presentsWithTransaction is NOT set here by default.
        // It breaks vsync frame pacing (causes ~240fps on 120Hz display).
        // Use SDL_SetWGPUSurfacePresentsWithTransaction() to enable it
        // temporarily during resize for smooth resize behavior.
        [content_view setLayer : metal_layer];

        // When the view's size changes before a new drawable is presented (window
        // resize / zoom animation, fullscreen transition, host resizing a plugin
        // view), Core Animation would by default scale the previous drawable to
        // fill the new bounds, which shows up as a visibly stretched frame. Pin the
        // contents to the top-left at their native scale instead, so stale content
        // stays crisp and in place until the next frame arrives at the new size.
        content_view.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;
        [(CAMetalLayer *)metal_layer setContentsGravity:kCAGravityTopLeft];

        WGPUSurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromMetalLayer.chain.next = NULL;
        fromMetalLayer.layer = metal_layer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;
        surfaceDescriptor.label = (WGPUStringView){ NULL, WGPU_STRLEN };

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif defined(SDL_PLATFORM_IOS)
    {
        UIWindow *ui_window = (__bridge UIWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
        CAMetalLayer* metal_layer;

        if (!ui_window) {
            // Embedded (parent-view) mode: the SDL content view is metal-backed
            // (SDL_uikitmetalview), so its own layer is the CAMetalLayer.
            UIView *content_view = (__bridge UIView *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_CONTENTVIEW_POINTER, NULL);
            if (!content_view || ![content_view.layer isKindOfClass:[CAMetalLayer class]]) return NULL;
            metal_layer = (CAMetalLayer *)content_view.layer;
        } else {
            UIView* ui_view = ui_window.rootViewController.view;
            metal_layer = [CAMetalLayer new];
            metal_layer.opaque = true;
            metal_layer.frame = ui_view.frame;
            metal_layer.drawableSize = ui_view.frame.size;

            [ui_view.layer addSublayer: metal_layer];
        }

        WGPUSurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromMetalLayer.chain.next = NULL;
        fromMetalLayer.layer = metal_layer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;
        surfaceDescriptor.label = (WGPUStringView){ NULL, WGPU_STRLEN };

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif defined(SDL_PLATFORM_LINUX)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        void *x11_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        uint64_t x11_window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (!x11_display || !x11_window) return NULL;

        WGPUSurfaceSourceXlibWindow fromXlibWindow;
        fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        fromXlibWindow.chain.next = NULL;
        fromXlibWindow.display = x11_display;
        fromXlibWindow.window = x11_window;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromXlibWindow.chain;
        surfaceDescriptor.label = (WGPUStringView){ NULL, WGPU_STRLEN };

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
    else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        void *wayland_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        void *wayland_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
        if (!wayland_display || !wayland_surface) return NULL;

        WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
        fromWaylandSurface.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        fromWaylandSurface.chain.next = NULL;
        fromWaylandSurface.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        fromWaylandSurface.surface = wayland_surface;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWaylandSurface.chain;
        surfaceDescriptor.label = (WGPUStringView){ NULL, WGPU_STRLEN };

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif defined(SDL_PLATFORM_WIN32)
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (!hwnd) return NULL;
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUSurfaceSourceWindowsHWND fromWindowsHWND;
        fromWindowsHWND.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        fromWindowsHWND.chain.next = NULL;
        fromWindowsHWND.hinstance = hinstance;
        fromWindowsHWND.hwnd = hwnd;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWindowsHWND.chain;
        surfaceDescriptor.label = (WGPUStringView){ NULL, WGPU_STRLEN };

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif defined(__EMSCRIPTEN__)
    {
#  ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.next = NULL;
        fromCanvasHTMLSelector.selector = (WGPUStringView){ "#canvas", 7 };

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
        surfaceDescriptor.label = (WGPUStringView){ NULL, 0 };
#  else
        WGPUSurfaceDescriptorFromCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.next = NULL;
        fromCanvasHTMLSelector.selector = "#canvas";

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
        surfaceDescriptor.label = NULL;
#  endif

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }  
#else
    // TODO: See SDL_syswm.h for other possible enum values!
#error "Unsupported WGPU_TARGET"
#endif
    return NULL;
}

void SDL_SetWGPUSurfacePresentsWithTransaction(SDL_Window* window, bool enabled) {
#if defined(SDL_PLATFORM_MACOS)
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    NSView *content_view = NULL;

    // Try to get the NSWindow first (standalone window mode)
    NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (ns_window) {
        content_view = ns_window.contentView;
    } else {
        // Embedded mode: no NSWindow, get the content view directly
        content_view = (__bridge NSView *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_CONTENTVIEW_POINTER, NULL);
    }

    if (!content_view) return;

    CALayer* layer = content_view.layer;
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
        if (!enabled && metalLayer.presentsWithTransaction) {
            // Commit any present still waiting on a transaction, so that drawable
            // is recycled before frames go back to presenting immediately
            [CATransaction flush];
        }
        [metalLayer setPresentsWithTransaction:enabled];
    }
#else
    (void)window;
    (void)enabled;
#endif
}

bool SDL_GetWGPUSurfaceInLiveResize(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS)
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    NSView *content_view = NULL;

    // Try to get the NSWindow first (standalone window mode)
    NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (ns_window) {
        content_view = ns_window.contentView;
    } else {
        // Embedded mode: no NSWindow, get the content view directly
        content_view = (__bridge NSView *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_CONTENTVIEW_POINTER, NULL);
    }

    return content_view != NULL && content_view.inLiveResize;
#else
    (void)window;
    return false;
#endif
}

bool SDL_GetWGPUSurfaceInFullscreenTransition(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS)
    // AppKit state may only be touched on the main thread (threaded render
    // loops can't stall the UI in nextDrawable anyway)
    if (![NSThread isMainThread]) return false;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!ns_window) return false;  // Embedded: the host owns the window

    // SDL's window listener (installed as the NSWindow delegate) tracks the
    // fullscreen Space transition from windowWillEnter/ExitFullScreen until
    // windowDidEnter/ExitFullScreen (or the DidFail variants).
    id delegate = ns_window.delegate;
    SEL sel = NSSelectorFromString(@"isInFullscreenSpaceTransition");
    if (delegate && [delegate respondsToSelector:sel]) {
        return ((BOOL (*)(id, SEL))[delegate methodForSelector:sel])(delegate, sel) == YES;
    }
    return false;
#else
    (void)window;
    return false;
#endif
}

void SDL_SyncWGPUSurfaceContentsScale(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS)
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    NSView *content_view = NULL;
    NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (ns_window) {
        content_view = ns_window.contentView;
    } else {
        // Embedded mode: no NSWindow, get the content view directly
        content_view = (__bridge NSView *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_CONTENTVIEW_POINTER, NULL);
    }
    if (!content_view || ![content_view.layer isKindOfClass:[CAMetalLayer class]]) return;

    // Without SDL_WINDOW_HIGH_PIXEL_DENSITY the drawable is sized in points
    CGFloat scale = 1.0;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_HIGH_PIXEL_DENSITY) {
        NSWindow *host_window = content_view.window;  // Host window when embedded
        if (host_window) {
            scale = host_window.backingScaleFactor;
        } else if ([NSScreen mainScreen]) {
            scale = [NSScreen mainScreen].backingScaleFactor;
        }
        if (scale <= 0.0) scale = 1.0;
    }

    CAMetalLayer *metal_layer = (CAMetalLayer *)content_view.layer;
    if (metal_layer.contentsScale != scale) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metal_layer.contentsScale = scale;
        [CATransaction commit];
    }
#else
    (void)window;
#endif
}

#if defined(SDL_PLATFORM_MACOS)
static void sdlwgpu_setLayerHidden(NSWindow* ns_window, bool hidden, bool flush) {
    if (![ns_window.contentView.layer isKindOfClass:[CAMetalLayer class]]) return;
    CAMetalLayer *metal_layer = (CAMetalLayer *)ns_window.contentView.layer;
    if (metal_layer.hidden == hidden) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    metal_layer.hidden = hidden;
    [CATransaction commit];
    if (flush) [CATransaction flush];
}
#endif

void SDL_SetWGPUSurfaceContentHidden(SDL_Window* window, bool hidden) {
#if defined(SDL_PLATFORM_MACOS)
    if (![NSThread isMainThread]) return;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (ns_window) sdlwgpu_setLayerHidden(ns_window, hidden, false);
#else
    (void)window; (void)hidden;
#endif
}

#if defined(SDL_PLATFORM_MACOS)
// toggleFullScreen: wrapper (see SDL_HideWGPUSurfaceOnFullscreenTransition)
static IMP sdlwgpu_originalToggleFullScreen = NULL;
static char kTogglePendingKey;

static bool sdlwgpu_isInFullscreenTransition(NSWindow* window) {
    id delegate = window.delegate;
    SEL sel = NSSelectorFromString(@"isInFullscreenSpaceTransition");
    return delegate && [delegate respondsToSelector:sel] &&
           ((BOOL (*)(id, SEL))[delegate methodForSelector:sel])(delegate, sel) == YES;
}

static void sdlwgpu_toggleFullScreen(id self, SEL _cmd, id sender) {
    NSWindow* window = (NSWindow*)self;
    void (*original)(id, SEL, id) = (void (*)(id, SEL, id))sdlwgpu_originalToggleFullScreen;
    if (objc_getAssociatedObject(window, &kTogglePendingKey)) {
        return;  // Already toggling (e.g. a double click on the green button)
    }
    // SDL_SetWindowFullscreen marks its transition as started before calling
    // this; let those (and windows without our layer) through untouched
    if (![window.contentView.layer isKindOfClass:[CAMetalLayer class]] ||
        sdlwgpu_isInFullscreenTransition(window)) {
        original(self, _cmd, sender);
        return;
    }
    // Blank the window and give the window server a couple of refreshes to show
    // it before AppKit takes the "before" snapshot for the transition animation
    sdlwgpu_setLayerHidden(window, true, true);
    objc_setAssociatedObject(window, &kTogglePendingKey, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [window retain];
    [sender retain];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 34 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        objc_setAssociatedObject(window, &kTogglePendingKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        original(window, _cmd, sender);
        // No transition started (e.g. fullscreen not allowed): show the content
        // again. Otherwise the app reveals it once the transition is over.
        if (!sdlwgpu_isInFullscreenTransition(window)) {
            sdlwgpu_setLayerHidden(window, false, false);
        }
        [sender release];
        [window release];
    });
}
#endif

void SDL_HideWGPUSurfaceOnFullscreenTransition(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS)
    if (![NSThread isMainThread]) return;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!ns_window) return;  // Embedded: the host owns the window
    static const char *kInstalledKey = "SDL_WGPU_hide_on_fullscreen_transition";
    if (SDL_GetBooleanProperty(props, kInstalledKey, false)) return;
    SDL_SetBooleanProperty(props, kInstalledKey, true);

    // Wrap toggleFullScreen: (green button, View > Enter Full Screen, ctrl-cmd-F
    // and SDL_SetWindowFullscreen all go through it) to blank the window before
    // AppKit's "before" snapshot, which it captures ahead of the notifications.
    Class cls = [ns_window class];
    SEL toggle = @selector(toggleFullScreen:);
    if (!sdlwgpu_originalToggleFullScreen) {
        Method m = class_getInstanceMethod(cls, toggle);
        sdlwgpu_originalToggleFullScreen = method_getImplementation(m);
        if (!class_addMethod(cls, toggle, (IMP)sdlwgpu_toggleFullScreen, method_getTypeEncoding(m))) {
            method_setImplementation(m, (IMP)sdlwgpu_toggleFullScreen);
        }
    }

    // Also hide at the "will" notifications, before the app's next frame, for
    // transitions that skip the wrapper's blanking (e.g. SDL's own), which keeps
    // the content out of AppKit's "after" snapshot. The block uses note.object
    // rather than capturing the window, which would retain it (this file is not
    // compiled with ARC); the observers are object-specific, so they never fire
    // once the window is gone.
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    void (^hide)(NSNotification *) = ^(NSNotification *note) {
        sdlwgpu_setLayerHidden((NSWindow *)note.object, true, true);
    };
    [center addObserverForName:NSWindowWillEnterFullScreenNotification object:ns_window
                         queue:nil usingBlock:hide];
    [center addObserverForName:NSWindowWillExitFullScreenNotification object:ns_window
                         queue:nil usingBlock:hide];
#else
    (void)window;
#endif
}
