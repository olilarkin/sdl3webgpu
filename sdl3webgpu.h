/**
 * This is an extension of SDL3 for WebGPU, abstracting away the details of
 * OS-specific operations.
 * 
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://eliemichel.github.io/LearnWebGPU
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

#ifndef _sdl3_webgpu_h_
#define _sdl3_webgpu_h_

#include <webgpu/webgpu.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get a WGPUSurface from a SDL3 window.
 */
WGPUSurface SDL_GetWGPUSurface(WGPUInstance instance, SDL_Window* window);

/**
 * Set presentsWithTransaction on the Metal layer for smooth resize (macOS only).
 *
 * When enabled, frame presentation is synchronized with Core Animation transactions,
 * which prevents visual glitches during window resize. However, this breaks vsync
 * frame pacing, so it should only be enabled during active resize operations.
 *
 * On non-macOS platforms, this function does nothing.
 */
void SDL_SetWGPUSurfacePresentsWithTransaction(SDL_Window* window, bool enabled);

/**
 * Query whether the window's content view is in an interactive live resize
 * (user dragging a window edge/corner). macOS only.
 *
 * Fullscreen Space animations, zoom/maximize, minimize and programmatic
 * resizes are NOT live resize. Use this to decide when presentsWithTransaction
 * is safe/beneficial: during live resize AppKit commits a Core Animation
 * transaction every tick of its modal resize loop, so transaction-synchronized
 * drawables are recycled each frame.
 *
 * On non-macOS platforms, always returns false.
 */
bool SDL_GetWGPUSurfaceInLiveResize(SDL_Window* window);

/**
 * Match the Metal layer's contentsScale to the window's backing scale factor
 * (macOS only). Call after (re)configuring the surface.
 *
 * The layer pins its contents top-left at native scale (so stale frames are
 * never stretched during resizes); that requires contentsScale to equal the
 * drawable-pixels-per-point ratio, which AppKit does not maintain for a custom
 * layer: the backing scale factor (2.0 on Retina, 1.0 on a non-Retina display)
 * for SDL_WINDOW_HIGH_PIXEL_DENSITY windows, otherwise 1.0.
 *
 * On non-macOS platforms, this function does nothing.
 */
void SDL_SyncWGPUSurfaceContentsScale(SDL_Window* window);

/**
 * Query whether the window is animating into or out of a fullscreen Space
 * (green button / Toggle Full Screen). macOS only.
 *
 * During the transition the window server holds on to the presented drawables
 * while it animates, so the CAMetalLayer drawable pool runs dry and
 * -[CAMetalLayer nextDrawable] blocks for its full 1 second timeout. The
 * window server animates a scaled copy of the last frame instead (and SDL
 * suppresses resize events until the transition ends), so callers should skip
 * rendering while this returns true.
 *
 * Returns false on non-macOS platforms, off the main thread, and for windows
 * without an SDL Cocoa delegate (embedded views).
 */
bool SDL_GetWGPUSurfaceInFullscreenTransition(SDL_Window* window);

#ifdef __cplusplus
}
#endif

#endif // _sdl3_webgpu_h_
