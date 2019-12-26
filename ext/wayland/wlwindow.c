/* GStreamer Wayland video sink
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2014 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "wlwindow.h"
#include "wlshmallocator.h"
#include "wlbuffer.h"

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

G_DEFINE_TYPE (GstWlWindow, gst_wl_window, G_TYPE_OBJECT);

static void gst_wl_window_finalize (GObject * gobject);

static void
handle_ping (void *data, struct wl_shell_surface *shell_surface,
    uint32_t serial)
{
  wl_shell_surface_pong (shell_surface, serial);
}

static void
handle_configure (void *data, struct wl_shell_surface *shell_surface,
    uint32_t edges, int32_t width, int32_t height)
{
  GstWlWindow *window = data;

  GST_DEBUG ("Windows configure: edges %x, width = %i, height %i", edges,
      width, height);

  if (width == 0 || height == 0)
    return;

  gst_wl_window_set_render_rectangle (window, 0, 0, width, height);
}

static void
handle_popup_done (void *data, struct wl_shell_surface *shell_surface)
{
  GST_DEBUG ("Window popup done.");
}

static const struct wl_shell_surface_listener shell_surface_listener = {
  handle_ping,
  handle_configure,
  handle_popup_done
};

static void
gst_wl_window_class_init (GstWlWindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_window_finalize;
}

static void
gst_wl_window_init (GstWlWindow * self)
{
}

static void
gst_wl_window_finalize (GObject * gobject)
{
  GstWlWindow *self = GST_WL_WINDOW (gobject);

  if (self->shell_surface) {
    wl_shell_surface_destroy (self->shell_surface);
    wl_surface_destroy (self->surface);
  }

  if (self->viewport)
    wp_viewport_destroy (self->viewport);

  if (self->surface_wrapper)
    wl_proxy_wrapper_destroy (self->surface_wrapper);

  g_clear_object (&self->display);

  G_OBJECT_CLASS (gst_wl_window_parent_class)->finalize (gobject);
}

static GstWlWindow *
gst_wl_window_new_internal (GstWlDisplay * display, struct wl_surface *surface,
    GMutex * render_lock)
{
  GstWlWindow *window;
  struct wl_region *region;

  window = g_object_new (GST_TYPE_WL_WINDOW, NULL);
  window->display = g_object_ref (display);
  window->render_lock = render_lock;

  window->surface = surface;
  window->surface_wrapper = wl_proxy_create_wrapper (surface);

  wl_proxy_set_queue ((struct wl_proxy *) window->surface_wrapper,
      display->queue);

  if (display->viewporter) {
    window->viewport = wp_viewporter_get_viewport (display->viewporter,
        window->surface);
  }

  /* do not accept input */
  region = wl_compositor_create_region (display->compositor);
  wl_surface_set_input_region (window->surface, region);
  wl_region_destroy (region);

  return window;
}

GstWlWindow *
gst_wl_window_new_toplevel (GstWlDisplay * display, const GstVideoInfo * info,
    GMutex * render_lock)
{
  GstWlWindow *window;
  gint width;
  struct wl_surface *surface;

  surface = wl_compositor_create_surface (display->compositor);
  window = gst_wl_window_new_internal (display, surface, render_lock);

  /* go toplevel */
  window->shell_surface = wl_shell_get_shell_surface (display->shell,
      window->surface);

  if (window->shell_surface) {
    wl_shell_surface_add_listener (window->shell_surface,
        &shell_surface_listener, window);
    wl_shell_surface_set_toplevel (window->shell_surface);
  } else {
    GST_ERROR ("Unable to get wl_shell_surface");

    g_object_unref (window);
    return NULL;
  }

  /* set the initial size to be the same as the reported video size */
  width =
      gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
  gst_wl_window_set_render_rectangle (window, 0, 0, width, info->height);

  return window;
}

GstWlWindow *
gst_wl_window_new_in_surface (GstWlDisplay * display,
    struct wl_surface * parent, GMutex * render_lock)
{
  GstWlWindow *window;
  /* Since webos does not support subsurface, use the parent surface as it is. */
  window = gst_wl_window_new_internal (display, parent, render_lock);
  return window;
}

GstWlDisplay *
gst_wl_window_get_display (GstWlWindow * window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return g_object_ref (window->display);
}

struct wl_surface *
gst_wl_window_get_wl_surface (GstWlWindow * window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return window->surface_wrapper;
}

gboolean
gst_wl_window_is_toplevel (GstWlWindow * window)
{
  g_return_val_if_fail (window != NULL, FALSE);

  return (window->shell_surface != NULL);
}

static void
gst_wl_window_resize_video_surface (GstWlWindow * window, gboolean commit)
{
  GstVideoRectangle src = { 0, };
  GstVideoRectangle dst = { 0, };
  GstVideoRectangle res;

  /* center the subsurface */
  src.w = window->video_width;
  src.h = window->video_height;
  dst.w = window->render_rectangle.w;
  dst.h = window->render_rectangle.h;

  if (window->viewport) {
    gst_video_sink_center_rect (src, dst, &res, TRUE);
    wp_viewport_set_destination (window->viewport, res.w, res.h);
  } else {
    gst_video_sink_center_rect (src, dst, &res, FALSE);
  }

  if (commit) {
    wl_surface_damage (window->surface_wrapper, res.x, res.y, res.w, res.h);
    wl_surface_commit (window->surface_wrapper);
  }

  if (gst_wl_window_is_toplevel (window)) {
    struct wl_region *region;

    region = wl_compositor_create_region (window->display->compositor);
    wl_region_add (region, 0, 0, window->render_rectangle.w,
        window->render_rectangle.h);
    wl_surface_set_input_region (window->surface, region);
    wl_region_destroy (region);
  }

  /* this is saved for use in wl_surface_damage */
  window->video_rectangle = res;
}

static void
gst_wl_window_set_opaque (GstWlWindow * window, const GstVideoInfo * info)
{
  struct wl_region *region;

  if (!GST_VIDEO_INFO_HAS_ALPHA (info)) {
    /* Set opaque */
    region = wl_compositor_create_region (window->display->compositor);
    wl_region_add (region, 0, 0, window->render_rectangle.w,
        window->render_rectangle.h);
    wl_surface_set_opaque_region (window->surface, region);
    wl_region_destroy (region);
  }
}

void
gst_wl_window_render (GstWlWindow * window, GstWlBuffer * buffer,
    const GstVideoInfo * info)
{
  if (G_UNLIKELY (info)) {
    window->video_width =
        gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
    window->video_height = info->height;

    gst_wl_window_resize_video_surface (window, FALSE);
    gst_wl_window_set_opaque (window, info);
  }

  if (G_LIKELY (buffer))
    gst_wl_buffer_attach (buffer, window->surface_wrapper);
  else
    wl_surface_attach (window->surface_wrapper, NULL, 0, 0);

  wl_surface_damage (window->surface_wrapper, 0, 0, window->video_rectangle.w,
      window->video_rectangle.h);
  wl_surface_commit (window->surface_wrapper);

  wl_display_flush (window->display->display);
}

/* Update the buffer used to draw black borders. When we have viewporter
 * support, this is a scaled up 1x1 image, and without we need an black image
 * the size of the rendering areay. */
static void
gst_wl_window_update_borders (GstWlWindow * window)
{
  GstVideoFormat format;
  GstVideoInfo info;
  gint width, height;
  GstBuffer *buf;
  struct wl_buffer *wlbuf;
  GstWlBuffer *gwlbuf;
  GstAllocator *alloc;

  if (window->no_border_update)
    return;

  if (window->display->viewporter) {
    width = height = 1;
    window->no_border_update = TRUE;
  } else {
    width = window->render_rectangle.w;
    height = window->render_rectangle.h;
  }

  /* we want WL_SHM_FORMAT_XRGB8888 */
#if G_BYTE_ORDER == G_BIG_ENDIAN
  format = GST_VIDEO_FORMAT_xRGB;
#else
  format = GST_VIDEO_FORMAT_BGRx;
#endif

  /* draw the area_subsurface */
  gst_video_info_set_format (&info, format, width, height);

  alloc = gst_wl_shm_allocator_get ();

  buf = gst_buffer_new_allocate (alloc, info.size, NULL);
  gst_buffer_memset (buf, 0, 0, info.size);
  wlbuf =
      gst_wl_shm_memory_construct_wl_buffer (gst_buffer_peek_memory (buf, 0),
      window->display, &info);
  gwlbuf = gst_buffer_add_wl_buffer (buf, wlbuf, window->display);
  gst_wl_buffer_attach (gwlbuf, window->surface_wrapper);

  /* at this point, the GstWlBuffer keeps the buffer
   * alive and will free it on wl_buffer::release */
  gst_buffer_unref (buf);
  g_object_unref (alloc);
}

void
gst_wl_window_set_render_rectangle (GstWlWindow * window, gint x, gint y,
    gint w, gint h)
{
  g_return_if_fail (window != NULL);

  window->render_rectangle.x = x;
  window->render_rectangle.y = y;
  window->render_rectangle.w = w;
  window->render_rectangle.h = h;

  /* change the size of the area */
  if (window->viewport)
    wp_viewport_set_destination (window->viewport, w, h);

  gst_wl_window_update_borders (window);

  if (window->video_width != 0) {
    gst_wl_window_resize_video_surface (window, TRUE);
  }

  wl_surface_damage (window->surface_wrapper, 0, 0, w, h);
  wl_surface_commit (window->surface_wrapper);
}
