/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
 *
 * Gsthlsdemux.c:
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-hlsdemux
 * @title: hlsdemux
 *
 * HTTP Live Streaming demuxer element.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 souphttpsrc location=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8 ! hlsdemux ! decodebin ! videoconvert ! videoscale ! autovideosink
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/tag/tag.h>
#include <gst/basedrm/gstbasedrm.h>
#include "gsthlsdemux.h"
#include <glib/gprintf.h>

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-hls"));

GST_DEBUG_CATEGORY (gst_hls_demux_debug);
#define GST_CAT_DEFAULT gst_hls_demux_debug

#define GST_M3U8_CLIENT_LOCK(l) /* FIXME */
#define GST_M3U8_CLIENT_UNLOCK(l)       /* FIXME */

/* GObject */
static void gst_hls_demux_finalize (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

/* GstHLSDemux */
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean update, GError ** err);
static gchar *gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf);

/* FIXME: the return value is never used? */
static gboolean gst_hls_demux_change_playlist (GstHLSDemux * demux,
    guint max_bitrate, gboolean * changed);
static GstBuffer *gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstHLSDemuxStream * stream, GstBuffer * encrypted_buffer, GError ** err);
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data);
static void gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream);

static gboolean gst_hls_demux_is_live (GstAdaptiveDemux * demux);
static GstClockTime gst_hls_demux_get_duration (GstAdaptiveDemux * demux);
static gint64 gst_hls_demux_get_manifest_update_interval (GstAdaptiveDemux *
    demux);
static gboolean gst_hls_demux_process_manifest (GstAdaptiveDemux * demux,
    GstBuffer * buf);
static GstFlowReturn gst_hls_demux_update_manifest (GstAdaptiveDemux * demux);
static gboolean gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek);
static GstFlowReturn gst_hls_demux_stream_seek (GstAdaptiveDemuxStream *
    stream, gboolean forward, GstSeekFlags flags, GstClockTime ts,
    GstClockTime * final_ts);
static gboolean
gst_hls_demux_start_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn gst_hls_demux_finish_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn gst_hls_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer);
static void gst_hls_demux_stream_free (GstAdaptiveDemuxStream * stream);
static gboolean gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemuxStream *
    stream);
static GstFlowReturn gst_hls_demux_advance_fragment (GstAdaptiveDemuxStream *
    stream);
static GstFlowReturn gst_hls_demux_update_fragment_info (GstAdaptiveDemuxStream
    * stream);
static gboolean gst_hls_demux_select_bitrate (GstAdaptiveDemuxStream * stream,
    guint64 bitrate);
static void gst_hls_demux_reset (GstAdaptiveDemux * demux);
static gboolean gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * start, gint64 * stop);
static GstM3U8 *gst_hls_demux_stream_get_m3u8 (GstHLSDemuxStream * hls_stream);
static void
gst_hls_demux_stream_set_m3u8 (GstHLSDemuxStream * hlsdemux_stream,
    GstM3U8 * m3u8);
static void gst_hls_demux_set_current_variant (GstHLSDemux * hlsdemux,
    GstHLSVariantStream * variant);
static GstClockTime gst_hls_demux_get_presentation_offset (GstAdaptiveDemux *
    demux, GstAdaptiveDemuxStream * stream);
static guint gst_hls_demux_select_initial_bitrate (GstAdaptiveDemux * demux,
    gint default_bandwdith);
static void gst_hls_demux_notify_adaptive_streaming_resource (GstAdaptiveDemux *
    demux);
static gboolean gst_hls_demux_setup_streams (GstAdaptiveDemux * demux, gboolean
    bitrate_changed);
static void gst_hls_demux_handle_sink_pad_linked (GstAdaptiveDemux *
    adaptivedemux, GstPad * pad, GstPad * peer);

static struct _DRMFunc
{
  void *(*drm_load) (const char *drm_type, const char *drm_client_id,
      const char *uri);
  int (*drm_get_key_from_url) (void *ctrl_handle, char *uri, char *key, ...);
  int (*drm_release) (void *ctrl_handle);
} drm_func;

#define gst_hls_demux_parent_class parent_class
G_DEFINE_TYPE (GstHLSDemux, gst_hls_demux, GST_TYPE_ADAPTIVE_DEMUX);

static void
gst_hls_demux_finalize (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
  g_mutex_clear (&demux->keys_lock);
  if (demux->keys) {
    g_hash_table_unref (demux->keys);
    demux->keys = NULL;
  }

  if (demux->drm_ctrl_handle) {
    drm_func.drm_release (demux->drm_ctrl_handle);
    demux->drm_ctrl_handle = NULL;
  }

  if (demux->module_drmcontroller)
    g_module_close (demux->module_drmcontroller);

  g_free (demux->drm_mediauri);
  g_free (demux->drm_clientid);
  g_free (demux->drm_type);
  g_free (demux->drm_systemid);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_hls_demux_class_init (GstHLSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstAdaptiveDemuxClass *adaptivedemux_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  adaptivedemux_class = (GstAdaptiveDemuxClass *) klass;

  gobject_class->finalize = gst_hls_demux_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_static_metadata (element_class,
      "HLS Demuxer",
      "Codec/Demuxer/Adaptive",
      "HTTP Live Streaming demuxer",
      "Marc-Andre Lureau <marcandre.lureau@gmail.com>\n"
      "Andoni Morales Alastruey <ylatuya@gmail.com>");

  adaptivedemux_class->is_live = gst_hls_demux_is_live;
  adaptivedemux_class->get_live_seek_range = gst_hls_demux_get_live_seek_range;
  adaptivedemux_class->get_duration = gst_hls_demux_get_duration;
  adaptivedemux_class->get_manifest_update_interval =
      gst_hls_demux_get_manifest_update_interval;
  adaptivedemux_class->process_manifest = gst_hls_demux_process_manifest;
  adaptivedemux_class->update_manifest = gst_hls_demux_update_manifest;
  adaptivedemux_class->reset = gst_hls_demux_reset;
  adaptivedemux_class->seek = gst_hls_demux_seek;
  adaptivedemux_class->stream_seek = gst_hls_demux_stream_seek;
  adaptivedemux_class->stream_has_next_fragment =
      gst_hls_demux_stream_has_next_fragment;
  adaptivedemux_class->stream_advance_fragment = gst_hls_demux_advance_fragment;
  adaptivedemux_class->stream_update_fragment_info =
      gst_hls_demux_update_fragment_info;
  adaptivedemux_class->stream_select_bitrate = gst_hls_demux_select_bitrate;
  adaptivedemux_class->stream_free = gst_hls_demux_stream_free;

  adaptivedemux_class->start_fragment = gst_hls_demux_start_fragment;
  adaptivedemux_class->finish_fragment = gst_hls_demux_finish_fragment;
  adaptivedemux_class->data_received = gst_hls_demux_data_received;

  adaptivedemux_class->get_presentation_offset =
      gst_hls_demux_get_presentation_offset;

  adaptivedemux_class->notify_adaptive_streaming_resource =
      gst_hls_demux_notify_adaptive_streaming_resource;
  adaptivedemux_class->handle_sink_pad_linked =
      gst_hls_demux_handle_sink_pad_linked;

  GST_DEBUG_CATEGORY_INIT (gst_hls_demux_debug, "hlsdemux", 0,
      "hlsdemux element");
}

static void
gst_hls_demux_handle_sink_pad_linked (GstAdaptiveDemux * adaptivedemux,
    GstPad * pad, GstPad * peer)
{
  GstSmartPropertiesReturn ret;
  GstHLSDemux *demux = GST_HLS_DEMUX_CAST (adaptivedemux);
  /* Just for debugging */
  gboolean real_time = FALSE;

  ret =
      gst_element_get_smart_properties (GST_ELEMENT_CAST (demux), "real-time",
      &real_time, "drm-mediauri", &demux->drm_mediauri, "drm-clientid",
      &demux->drm_clientid, "drm-type", &demux->drm_type, "drm-systemid",
      &demux->drm_systemid, NULL);

  if (ret != GST_SMART_PROPERTIES_OK)
    GST_INFO_OBJECT (demux, "smart properties ret = %d", ret);

  GST_INFO_OBJECT (demux, "real-time : %d", real_time);
  GST_INFO_OBJECT (demux,
      "drm-mediauri : %s", GST_STR_NULL (demux->drm_mediauri));
  GST_INFO_OBJECT (demux,
      "drm-clientid : %s", GST_STR_NULL (demux->drm_clientid));
  GST_INFO_OBJECT (demux, "drm-type : %s", GST_STR_NULL (demux->drm_type));
  GST_INFO_OBJECT (demux,
      "drm-systemid : %s", GST_STR_NULL (demux->drm_systemid));

  if (demux->drm_clientid && !g_strcmp0 (demux->drm_type, "verimatrix")) {
    demux->module_drmcontroller =
        g_module_open ("/usr/lib/libdrmcontroller.so.1", G_MODULE_BIND_LAZY);
    if (!demux->module_drmcontroller)
      GST_ERROR_OBJECT (demux, "Failed to open a module: %s",
          g_module_error ());

    if (!g_module_symbol (demux->module_drmcontroller, "API_DRM_Load",
            (gpointer *) & drm_func.drm_load)) {
      GST_ERROR_OBJECT (demux, "Failed to get a symbol: %s", g_module_error ());
    }

    if (!g_module_symbol (demux->module_drmcontroller, "API_DRM_GetKeyFromUrl",
            (gpointer *) & drm_func.drm_get_key_from_url)) {
      GST_ERROR_OBJECT (demux, "Failed to get a symbol: %s", g_module_error ());
    }

    if (!g_module_symbol (demux->module_drmcontroller, "API_DRM_Release",
            (gpointer *) & drm_func.drm_release)) {
      GST_ERROR_OBJECT (demux, "Failed to get a symbol: %s", g_module_error ());
    }
    demux->drm_ctrl_handle =
        drm_func.drm_load ("viewright_web", demux->drm_clientid, NULL);

    if (!demux->drm_ctrl_handle)
      GST_ERROR_OBJECT (demux, "Failed to create drm controller handler");
  }
}

static void
gst_hls_demux_init (GstHLSDemux * demux)
{
  gst_adaptive_demux_set_stream_struct_size (GST_ADAPTIVE_DEMUX_CAST (demux),
      sizeof (GstHLSDemuxStream));

  demux->keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_mutex_init (&demux->keys_lock);
}

static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstHLSDemux *demux = GST_HLS_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
      g_hash_table_remove_all (demux->keys);
      break;
    default:
      break;
  }
  return ret;
}

static GstPad *
gst_hls_demux_create_pad (GstHLSDemux * hlsdemux)
{
  gchar *name;
  GstPad *pad;

  name = g_strdup_printf ("src_%u", hlsdemux->srcpad_counter++);
  pad = gst_pad_new_from_static_template (&srctemplate, name);
  g_free (name);

  return pad;
}

static guint64
gst_hls_demux_get_bitrate (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (hlsdemux);

  /* Valid because hlsdemux only has a single output */
  if (demux->streams) {
    GstAdaptiveDemuxStream *stream = demux->streams->data;
    return demux->connection_speed >
        0 ? demux->connection_speed : stream->current_download_rate;
  }

  return 0;
}

static void
gst_hls_demux_stream_clear_pending_data (GstHLSDemuxStream * hls_stream)
{
  if (hls_stream->pending_encrypted_data)
    gst_adapter_clear (hls_stream->pending_encrypted_data);
  gst_buffer_replace (&hls_stream->pending_decrypted_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_typefind_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_pcr_buffer, NULL);
  hls_stream->current_offset = -1;
  gst_hls_demux_stream_decrypt_end (hls_stream);
  if (hls_stream->isobmff_adapter)
    gst_adapter_clear (hls_stream->isobmff_adapter);
  hls_stream->isobmff_parser.current_fourcc = 0;
  hls_stream->isobmff_parser.current_start_offset = 0;
  hls_stream->isobmff_parser.current_offset = 0;
  hls_stream->isobmff_parser.current_size = 0;

  if (hls_stream->moof)
    gst_isoff_moof_box_free (hls_stream->moof);
  hls_stream->moof = NULL;
  if (!hls_stream->find_presentation_offset)
    gst_buffer_replace (&hls_stream->pending_pts_buffer, NULL);
}

static void
gst_hls_demux_clear_all_pending_data (GstHLSDemux * hlsdemux,
    gboolean clear_static)
{
  GstAdaptiveDemux *demux = (GstAdaptiveDemux *) hlsdemux;
  GList *walk;

  for (walk = demux->streams; walk != NULL; walk = walk->next) {
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (walk->data);
    GstAdaptiveDemuxStream *stream = (GstAdaptiveDemuxStream *) hls_stream;
    if (stream->is_static && !clear_static) {
      GST_LOG_OBJECT (stream->pad, "Skip clear for static stream");
      continue;
    }
    gst_hls_demux_stream_clear_pending_data (hls_stream);
  }
}

#if 0
static void
gst_hls_demux_set_current (GstHLSDemux * self, GstM3U8 * m3u8)
{
  GST_M3U8_CLIENT_LOCK (self);
  if (m3u8 != self->current) {
    self->current = m3u8;
    self->current->duration = GST_CLOCK_TIME_NONE;
    self->current->current_file = NULL;

#if 0
    // FIXME: this makes no sense after we just set self->current=m3u8 above (tpm)
    // also, these values don't necessarily align between different lists
    m3u8->current_file_duration = self->current->current_file_duration;
    m3u8->sequence = self->current->sequence;
    m3u8->sequence_position = self->current->sequence_position;
    m3u8->highest_sequence_number = self->current->highest_sequence_number;
    m3u8->first_file_start = self->current->first_file_start;
    m3u8->last_file_end = self->current->last_file_end;
#endif
  }
  GST_M3U8_CLIENT_UNLOCK (self);
}
#endif

static gboolean
gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate, old_rate;
  GList *walk;
  GstClockTime target_pos;
  guint64 bitrate;
  gboolean do_snapseek = FALSE;

  gst_event_parse_seek (seek, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (!SEEK_UPDATES_PLAY_POSITION (rate, start_type, stop_type)) {
    /* nothing to do if we don't have to update the current position */
    return TRUE;
  }

  old_rate = demux->segment.rate;

  bitrate = gst_hls_demux_get_bitrate (hlsdemux);

  if (hlsdemux->master == NULL) {
    GST_WARNING ("Master is null.");
    return FALSE;
  }

  /* Use I-frame variants for trick modes */
  if (hlsdemux->master->iframe_variants != NULL
      && rate < -1.0 && old_rate >= -1.0 && old_rate <= 1.0) {
    GError *err = NULL;

    /* Switch to I-frame variant */
    gst_hls_demux_set_current_variant (hlsdemux,
        hlsdemux->master->iframe_variants->data);
    gst_uri_downloader_reset (demux->downloader);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;

    gst_hls_demux_change_playlist (hlsdemux, bitrate / ABS (rate), NULL);
  } else if (rate > -1.0 && rate <= 1.0 && (old_rate < -1.0 || old_rate > 1.0)) {
    GError *err = NULL;
    /* Switch to normal variant */
    gst_hls_demux_set_current_variant (hlsdemux,
        hlsdemux->master->variants->data);
    gst_uri_downloader_reset (demux->downloader);
    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;
    /* TODO why not continue using the same? that was being used up to now? */
    gst_hls_demux_change_playlist (hlsdemux, bitrate, NULL);
  } else {
    gboolean changed = FALSE;
    gst_hls_demux_change_playlist (hlsdemux, bitrate, &changed);
    if (changed)
      gst_hls_demux_setup_streams (GST_ADAPTIVE_DEMUX_CAST (hlsdemux), TRUE);
  }

  target_pos = rate < 0 ? stop : start;

  if (IS_SNAP_SEEK (flags))
    do_snapseek = FALSE;

  /* properly cleanup pending decryption status */
  if (flags & GST_SEEK_FLAG_FLUSH) {
    gst_hls_demux_clear_all_pending_data (hlsdemux, TRUE);
  }

  for (walk = demux->streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemuxStream *stream =
        GST_ADAPTIVE_DEMUX_STREAM_CAST (walk->data);

    if (do_snapseek) {
      GstClockTime final_ts;
      gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos,
          &final_ts);
      if (GST_CLOCK_TIME_IS_VALID (final_ts))
        target_pos = final_ts;
      do_snapseek = FALSE;
    } else
      gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos, NULL);
  }

  for (walk = demux->prepared_streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemuxStream *stream =
        GST_ADAPTIVE_DEMUX_STREAM_CAST (walk->data);

    if (do_snapseek) {
      GstClockTime final_ts;
      gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos,
          &final_ts);
      if (GST_CLOCK_TIME_IS_VALID (final_ts))
        target_pos = final_ts;
      do_snapseek = FALSE;
    } else
      gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos, NULL);
  }

  for (walk = demux->next_streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemuxStream *stream =
        GST_ADAPTIVE_DEMUX_STREAM_CAST (walk->data);

    if (do_snapseek) {
      GstClockTime final_ts;
      gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos,
          &final_ts);
      if (GST_CLOCK_TIME_IS_VALID (final_ts))
        target_pos = final_ts;
      do_snapseek = FALSE;
    } else
      gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos, NULL);
  }

  for (walk = demux->streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemuxStream *stream =
        GST_ADAPTIVE_DEMUX_STREAM_CAST (walk->data);
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
    GstStreamType stream_type = gst_stream_get_stream_type (stream->object);
    hls_stream->start_offset_after_seek =
        target_pos - hls_stream->playlist->sequence_position;
    GST_DEBUG_OBJECT (hlsdemux,
        "start_offset_after_seek %" GST_TIME_FORMAT " stream type %s"
        " target_pos: %" GST_TIME_FORMAT " sequence_position: %"
        GST_TIME_FORMAT, GST_TIME_ARGS (hls_stream->start_offset_after_seek),
        gst_stream_type_get_name (stream_type), GST_TIME_ARGS (target_pos),
        GST_TIME_ARGS (hls_stream->playlist->sequence_position));
  }

  for (walk = demux->prepared_streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemuxStream *stream =
        GST_ADAPTIVE_DEMUX_STREAM_CAST (walk->data);
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
    GstStreamType stream_type = gst_stream_get_stream_type (stream->object);
    hls_stream->start_offset_after_seek =
        target_pos - hls_stream->playlist->sequence_position;
    GST_DEBUG_OBJECT (hlsdemux,
        "start_offset_after_seek %" GST_TIME_FORMAT " stream type %s",
        GST_TIME_ARGS (hls_stream->start_offset_after_seek),
        gst_stream_type_get_name (stream_type));
  }

  for (walk = demux->next_streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemuxStream *stream =
        GST_ADAPTIVE_DEMUX_STREAM_CAST (walk->data);
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
    GstStreamType stream_type = gst_stream_get_stream_type (stream->object);
    hls_stream->start_offset_after_seek =
        target_pos - hls_stream->playlist->sequence_position;
    GST_DEBUG_OBJECT (hlsdemux,
        "start_offset_after_seek %" GST_TIME_FORMAT " stream type %s",
        GST_TIME_ARGS (hls_stream->start_offset_after_seek),
        gst_stream_type_get_name (stream_type));
  }

  if (IS_SNAP_SEEK (flags)) {
    if (rate >= 0)
      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          target_pos, stop_type, stop, NULL);
    else
      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          start, stop_type, target_pos, NULL);
  }

  return TRUE;
}

static GstFlowReturn
gst_hls_demux_stream_seek (GstAdaptiveDemuxStream * stream, gboolean forward,
    GstSeekFlags flags, GstClockTime ts, GstClockTime * final_ts)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GList *walk;
  GstClockTime current_pos;
  gint64 current_sequence;
  gboolean snap_after, snap_nearest;
  GstM3U8MediaFile *file = NULL;

  current_sequence = 0;
  current_pos = gst_m3u8_is_live (hls_stream->playlist) ?
      hls_stream->playlist->first_file_start : 0;

  if (gst_m3u8_is_live (hls_stream->playlist)) {
    if (forward && current_pos > ts) {
      ts = current_pos;
    } else if (!forward && hls_stream->playlist->last_file_end < ts) {
      ts = hls_stream->playlist->last_file_end;
    }
  }

  /* Snap to segment boundary. Improves seek performance on slow machines. */
  snap_nearest =
      (flags & GST_SEEK_FLAG_SNAP_NEAREST) == GST_SEEK_FLAG_SNAP_NEAREST;
  snap_after = ! !(flags & GST_SEEK_FLAG_SNAP_AFTER);

  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  /* FIXME: Here we need proper discont handling */
  for (walk = hls_stream->playlist->files; walk; walk = walk->next) {
    file = walk->data;

    current_sequence = file->sequence;
    if ((forward && snap_after) || snap_nearest) {
      if (current_pos >= ts)
        break;
      if (snap_nearest && ts - current_pos < file->duration / 2)
        break;
    } else if (!forward && snap_after) {
      /* check if the next fragment is our target, in this case we want to
       * start from the previous fragment */
      GstClockTime next_pos = current_pos + file->duration;

      if (next_pos <= ts && ts < next_pos + file->duration) {
        break;
      }
    } else if (current_pos <= ts && ts < current_pos + file->duration) {
      break;
    }
    current_pos += file->duration;
  }

  if (walk == NULL) {
    GST_DEBUG_OBJECT (stream->pad, "seeking further than track duration");
    current_sequence++;
  }

  GST_DEBUG_OBJECT (stream->pad, "seeking to sequence %u",
      (guint) current_sequence);
  hls_stream->reset_pts = TRUE;
  hls_stream->playlist->sequence = current_sequence;
  hls_stream->playlist->current_file = walk;
  hls_stream->playlist->sequence_position = current_pos;
  hls_stream->find_presentation_offset = TRUE;
  GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);

  /* Play from the end of the current selected segment */
  if (file) {
    if (!forward && IS_SNAP_SEEK (flags))
      current_pos += file->duration;
  }

  /* update stream's segment position */
  stream->segment.position = current_pos;

  if (final_ts)
    *final_ts = current_pos;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_update_manifest (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  if (!gst_hls_demux_update_playlist (hlsdemux, TRUE, NULL))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static void
gst_hls_demux_set_stream_type (GstHLSDemux * hlsdemux, GstHLSDemuxStream *
    stream, GstCaps * caps)
{
  GstStreamType type = GST_STREAM_TYPE_UNKNOWN;
  GstHLSMedia *media = stream->media;

  /* FIXME: it's work-around */
  if (!media) {
    /* This must be variant stream */
    type = GST_STREAM_TYPE_CONTAINER;
  } else {
    switch (media->mtype) {
      case GST_HLS_MEDIA_TYPE_AUDIO:
        type = GST_STREAM_TYPE_AUDIO;
        break;
      case GST_HLS_MEDIA_TYPE_VIDEO:
        type = GST_STREAM_TYPE_VIDEO;
        break;
      case GST_HLS_MEDIA_TYPE_SUBTITLES:
      case GST_HLS_MEDIA_TYPE_CLOSED_CAPTIONS:
        type = GST_STREAM_TYPE_TEXT;
        break;
      default:
        break;
    }
  }

  gst_adaptive_demux_stream_set_stream_type (GST_ADAPTIVE_DEMUX_STREAM_CAST
      (stream), type);
}

static void
create_stream_for_playlist (GstAdaptiveDemux * demux, GstM3U8 * playlist,
    GstHLSMedia * media, gboolean is_primary_playlist, gboolean selected)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstHLSDemuxStream *hlsdemux_stream;
  GstAdaptiveDemuxStream *stream;
  GstStreamFlags flags = GST_STREAM_FLAG_NONE;

#if 0
  if (!selected) {
    /* FIXME: Later, create the stream but mark not-selected */
    GST_LOG_OBJECT (demux, "Ignoring not-selected stream");
    return;
  }
#endif

  stream = gst_adaptive_demux_stream_new (demux,
      gst_hls_demux_create_pad (hlsdemux));
  stream->is_static = !is_primary_playlist;

  hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  hlsdemux_stream->stream_type = GST_HLS_TSREADER_NONE;

  hlsdemux_stream->playlist = gst_m3u8_ref (playlist);
  if (selected)
    flags |= GST_STREAM_FLAG_SELECT;

  if (media) {
    GstTagList *tags = gst_tag_list_new_empty ();

    if (!is_primary_playlist)
      hlsdemux_stream->media = gst_hls_media_ref (media);
    GstSample *sample;
    GstBuffer *buf = gst_buffer_new ();
    sample = gst_sample_new (buf, NULL, NULL,
        gst_structure_new ("hls-media-tag",
            "name", G_TYPE_STRING, media->name,
            "language", G_TYPE_STRING, media->lang,
            "channels", G_TYPE_INT, media->channels,
            "default", G_TYPE_BOOLEAN, media->is_default,
            "track-order", G_TYPE_INT, media->track_order, NULL));
    gst_buffer_unref (buf);

    GST_LOG_OBJECT (stream->pad,
        "Adding #EXT-X-MEDIA attributes to taglist. name: %s, language: %s, channels: %d, default: %d track-order: %d",
        media->name, media->lang, media->channels, media->is_default,
        media->track_order);

    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_APPLICATION_DATA, sample, NULL);
    gst_sample_unref (sample);

    gst_adaptive_demux_stream_set_tags (stream, tags);

    gst_tag_list_unref (tags);

    if (media->mtype == GST_HLS_MEDIA_TYPE_SUBTITLES ||
        media->mtype == GST_HLS_MEDIA_TYPE_CLOSED_CAPTIONS)
      flags |= GST_STREAM_FLAG_SPARSE;
    if (!is_primary_playlist && media->name) {
      playlist->media_name = g_strdup (media->name);
      GST_LOG_OBJECT (stream->pad,
          "Record name of rendition to rendition playlist (%s)",
          playlist->media_name);
      playlist->media_type = media->mtype;
    }
  }

  gst_adaptive_demux_stream_set_stream_flags (stream, flags);

  hlsdemux_stream->do_typefind = TRUE;
  hlsdemux_stream->reset_pts = TRUE;
}

static gboolean
gst_hls_demux_setup_streams (GstAdaptiveDemux * demux, gboolean bitrate_changed)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstHLSVariantStream *playlist = hlsdemux->current_variant;
  gint i;
  GstHLSMedia *muxed_media = NULL;

  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Can't configure streams - no variant selected");
    return FALSE;
  }

  gst_hls_demux_clear_all_pending_data (hlsdemux, bitrate_changed ?
      FALSE : TRUE);

  /* Find the media tag with no uri.
   * This is to set the attributes of the media tag as taglists to the stream */
  if (playlist->media[GST_HLS_MEDIA_TYPE_AUDIO]) {
    GList *mlist = playlist->media[GST_HLS_MEDIA_TYPE_AUDIO];
    GstHLSMedia *iter_media = NULL;
    while (mlist != NULL) {
      iter_media = mlist->data;
      if (iter_media->uri == NULL) {
        GST_LOG_OBJECT (demux, "Has muxed audio stream %s type %d",
            iter_media->name, iter_media->mtype);
        muxed_media = iter_media;
        break;
      }
      mlist = mlist->next;
    }
  }
  /* 1 output for the main playlist */
  /* FIXME: Which media should be used ? */
  create_stream_for_playlist (demux, playlist->m3u8, muxed_media, TRUE,
      playlist->assume_default);


  /* create only variant stream when bitrate changed */
  if (bitrate_changed)
    goto done;

  for (i = 0; i < GST_HLS_N_MEDIA_TYPES; ++i) {
    GList *mlist = playlist->media[i];
    while (mlist != NULL) {
      GstHLSMedia *media = mlist->data;

      if (media->uri == NULL /* || media->mtype != GST_HLS_MEDIA_TYPE_AUDIO */ ) {
        /* No uri means this is a placeholder for a stream
         * contained in another mux */
        GST_LOG_OBJECT (demux, "Skipping stream %s type %d with no URI",
            media->name, media->mtype);
        mlist = mlist->next;
        continue;
      }
      GST_LOG_OBJECT (demux, "media of type %d - %s, uri: %s", i,
          media->name, media->uri);
      create_stream_for_playlist (demux, media->playlist, media, FALSE,
          media->is_default);

      mlist = mlist->next;
    }
  }

done:
  return TRUE;
}

static const gchar *
gst_adaptive_demux_get_manifest_ref_uri (GstAdaptiveDemux * d)
{
  return d->manifest_base_uri ? d->manifest_base_uri : d->manifest_uri;
}

static void
gst_hls_demux_set_current_variant (GstHLSDemux * hlsdemux,
    GstHLSVariantStream * variant)
{
  if (hlsdemux->current_variant == variant || variant == NULL)
    return;

  if (hlsdemux->current_variant != NULL) {
    gint i;
    GstAdaptiveDemux *ad_demux = GST_ADAPTIVE_DEMUX_CAST (hlsdemux);
    GList *walk = ad_demux->streams;
    for (walk = ad_demux->streams; walk != NULL; walk = walk->next) {
      GstAdaptiveDemuxStream *ad_stream = walk->data;
      GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (ad_stream);
      GstStreamType stream_type =
          gst_stream_get_stream_type (ad_stream->object);
      GstM3U8 *m3u8 = gst_hls_demux_stream_get_m3u8 (hls_stream);

      if (hls_stream->stream_type != GST_HLS_TSREADER_FMP4) {
        GST_DEBUG_OBJECT (ad_stream->pad,
            "Only support group change for fmp4 type");
        break;
      }

      if (m3u8->media_name) {
        GST_DEBUG_OBJECT (ad_stream->pad, "media_name %s", m3u8->media_name);
        for (i = 0; i < GST_HLS_N_MEDIA_TYPES; ++i) {
          GList *mlist = variant->media[i];
          while (mlist != NULL) {
            GstHLSMedia *mapped_media = mlist->data;
            if (m3u8->media_type == mapped_media->mtype
                && !g_strcmp0 (m3u8->media_name, mapped_media->name)
                && g_strcmp0 (m3u8->uri, mapped_media->uri)) {
              GST_DEBUG_OBJECT (ad_stream->pad,
                  "Mapped rendition uris are different, groups must have changed. previous playlist %s -> new playlist %s",
                  m3u8->uri, mapped_media->uri);
              gst_hls_demux_stream_set_m3u8 (hls_stream,
                  mapped_media->playlist);
              g_free (hls_stream->playlist->media_name);
              hls_stream->playlist->media_name = g_strdup (mapped_media->name);
            }
            mlist = mlist->next;
          }
        }
      }
    }

    //#warning FIXME: Synching fragments across variants
    //  should be done based on media timestamps, and
    //  discont-sequence-numbers not sequence numbers.
    variant->m3u8->sequence_position =
        hlsdemux->current_variant->m3u8->sequence_position;
    variant->m3u8->sequence = hlsdemux->current_variant->m3u8->sequence;
    /* FIXME: As long as we sync variants using sequence number,
     * following variables should be copied to new variant stream for sync */
    variant->m3u8->highest_sequence_number =
        hlsdemux->current_variant->m3u8->highest_sequence_number;
    variant->m3u8->last_file_end =
        hlsdemux->current_variant->m3u8->last_file_end;
    variant->m3u8->first_file_start =
        hlsdemux->current_variant->m3u8->first_file_start;

    GST_DEBUG_OBJECT (hlsdemux,
        "Switching Variant. Copying over sequence %" G_GINT64_FORMAT
        " and sequence_pos %" GST_TIME_FORMAT, variant->m3u8->sequence,
        GST_TIME_ARGS (variant->m3u8->sequence_position));

    for (i = 0; i < GST_HLS_N_MEDIA_TYPES; ++i) {
      GList *mlist = hlsdemux->current_variant->media[i];

      while (mlist != NULL) {
        GstHLSMedia *old_media = mlist->data;
        GstHLSMedia *new_media =
            gst_hls_variant_find_matching_media (variant, old_media);

        if (new_media) {
          new_media->playlist->sequence = old_media->playlist->sequence;
          new_media->playlist->sequence_position =
              old_media->playlist->sequence_position;
          new_media->playlist->highest_sequence_number =
              old_media->playlist->highest_sequence_number;
          new_media->playlist->last_file_end =
              old_media->playlist->last_file_end;
          new_media->playlist->first_file_start =
              old_media->playlist->first_file_start;
          new_media->playlist->current_file_duration =
              old_media->playlist->current_file_duration;
        }
        mlist = mlist->next;
      }
    }

    gst_hls_variant_stream_unref (hlsdemux->current_variant);
  }

  hlsdemux->current_variant = gst_hls_variant_stream_ref (variant);

}

static guint
gst_hls_demux_select_initial_bitrate (GstAdaptiveDemux * demux, gint
    default_bandwidth)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  GST_INFO_OBJECT (demux,
      "connection-speed : %u, start-bitrate : %u, min-bitrate : %u, max-bitrate : %u",
      demux->connection_speed, demux->start_bitrate, demux->min_bitrate,
      demux->max_bitrate);

  if (demux->start_bitrate > 0)
    return gst_hls_master_playlist_get_initial_bitrate (hlsdemux->master, NULL,
        demux->start_bitrate, demux->min_bitrate);

  if (demux->min_bitrate == 0 && demux->max_bitrate == 0)
    return 0;

  if (demux->min_bitrate == 0) {
    if (demux->max_bitrate < default_bandwidth)
      return demux->max_bitrate;
  } else {
    if (demux->min_bitrate > default_bandwidth)
      return demux->min_bitrate;
  }
  return 0;
}

static gboolean
gst_hls_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstHLSVariantStream *variant;
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gchar *playlist = NULL;

  GST_INFO_OBJECT (demux, "Initial playlist location: %s (base uri: %s)",
      demux->manifest_uri, demux->manifest_base_uri);

  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Error validating initial playlist");
    return FALSE;
  }

  GST_M3U8_CLIENT_LOCK (self);
  hlsdemux->master = gst_hls_master_playlist_new_from_data (playlist,
      gst_adaptive_demux_get_manifest_ref_uri (demux));

  if (hlsdemux->master == NULL || hlsdemux->master->variants == NULL) {
    /* In most cases, this will happen if we set a wrong url in the
     * source element and we have received the 404 HTML response instead of
     * the playlist */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid playlist."),
        ("Could not parse playlist. Check if the URL is correct."));
    GST_M3U8_CLIENT_UNLOCK (self);
    return FALSE;
  }

  /* select the initial variant stream */
  if (demux->connection_speed == 0) {
    guint start_bitrate = gst_hls_demux_select_initial_bitrate (demux,
        hlsdemux->master->default_variant->bandwidth);
    if (start_bitrate > 0)
      variant =
          gst_hls_master_playlist_get_variant_for_bitrate (hlsdemux->master,
          NULL, start_bitrate);
    else
      variant = hlsdemux->master->default_variant;
  } else {
    variant =
        gst_hls_master_playlist_get_variant_for_bitrate (hlsdemux->master,
        NULL, demux->connection_speed);
  }

  if (variant) {
    GST_INFO_OBJECT (hlsdemux, "selected %s", variant->name);
    gst_hls_demux_set_current_variant (hlsdemux, variant);      // FIXME: inline?
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT_CAST (demux),
            gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                "bitrate", G_TYPE_INT, variant->bandwidth, NULL)));
  }

  /* get the selected media playlist (unless the inital list was one already) */
  if (!hlsdemux->master->is_simple) {
    GError *err = NULL;

    if (!gst_hls_demux_update_playlist (hlsdemux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not fetch media playlist",
          err);
      GST_M3U8_CLIENT_UNLOCK (self);
      return FALSE;
    }
  }
  GST_M3U8_CLIENT_UNLOCK (self);

  return gst_hls_demux_setup_streams (demux, FALSE);
}

static GstClockTime
gst_hls_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  if (hlsdemux->current_variant != NULL)
    duration = gst_m3u8_get_duration (hlsdemux->current_variant->m3u8);

  return duration;
}

static gboolean
gst_hls_demux_is_live (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean is_live = FALSE;

  if (hlsdemux->current_variant)
    is_live = gst_hls_variant_stream_is_live (hlsdemux->current_variant);

  return is_live;
}

static const GstHLSKey *
gst_hls_demux_get_key (GstHLSDemux * demux, const gchar * key_url,
    const gchar * referer, gboolean allow_cache)
{
  GstFragment *key_fragment;
  GstBuffer *key_buffer;
  GstHLSKey *key;
  GError *err = NULL;

  GST_LOG_OBJECT (demux, "Looking up key for key url %s", key_url);

  g_mutex_lock (&demux->keys_lock);

  key = g_hash_table_lookup (demux->keys, key_url);

  if (key != NULL) {
    GST_LOG_OBJECT (demux, "Found key for key url %s in key cache", key_url);
    goto out;
  }

  GST_INFO_OBJECT (demux, "Fetching key %s", key_url);

  key_fragment =
      gst_uri_downloader_fetch_uri (GST_ADAPTIVE_DEMUX (demux)->downloader,
      key_url, referer, GST_ADAPTIVE_DEMUX (demux)->user_agent,
      GST_ADAPTIVE_DEMUX (demux)->cookies, FALSE, FALSE, allow_cache, &err);

  if (key_fragment == NULL) {
    GST_WARNING_OBJECT (demux, "Failed to download key to decrypt data: %s",
        err ? err->message : "error");
    g_clear_error (&err);
    goto out;
  }

  key_buffer = gst_fragment_get_buffer (key_fragment);

  key = g_new0 (GstHLSKey, 1);
  if (gst_buffer_extract (key_buffer, 0, key->data, 16) < 16)
    GST_WARNING_OBJECT (demux, "Download decryption key is too short!");

  g_hash_table_insert (demux->keys, g_strdup (key_url), key);

  gst_buffer_unref (key_buffer);
  g_object_unref (key_fragment);

out:

  g_mutex_unlock (&demux->keys_lock);

  if (key != NULL)
    GST_MEMDUMP_OBJECT (demux, "Key", key->data, 16);

  return key;
}

static const GstHLSKey *
gst_hls_demux_get_key_from_drm_service (GstHLSDemux * demux,
    const gchar * key_url)
{
  GstHLSKey *key;

  GST_LOG_OBJECT (demux, "Retrieving key from drm service. key url %s",
      key_url);

  g_mutex_lock (&demux->keys_lock);
  key = g_hash_table_lookup (demux->keys, key_url);

  if (key != NULL) {
    GST_LOG_OBJECT (demux, "Found key for key url %s in key cache", key_url);
    goto out;
  }

  key = g_new0 (GstHLSKey, 1);

  if (!drm_func.drm_get_key_from_url (demux->drm_ctrl_handle, (char *) key_url,
          (char *) key->data))
    GST_LOG_OBJECT (demux, "Failed to get key from url");

  g_hash_table_insert (demux->keys, g_strdup (key_url), key);

out:
  g_mutex_unlock (&demux->keys_lock);

  return key;
}

static gboolean
gst_hls_demux_start_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  const GstHLSKey *key;
  GstM3U8 *m3u8;

  gst_hls_demux_stream_clear_pending_data (hls_stream);

  /* Init the timestamp reader for this fragment */
  gst_hlsdemux_tsreader_init (&hls_stream->tsreader);
  /* Reset the stream type if we already know it */
  gst_hlsdemux_tsreader_set_type (&hls_stream->tsreader,
      hls_stream->stream_type);

  hls_stream->isobmff_parser.current_offset = -1;

  /* If no decryption is needed, there's nothing to be done here */
  if (hls_stream->current_key == NULL) {
    /* For SAMPLE-AES method using cenc */
    if ((!g_strcmp0 (hlsdemux->drm_type, "widevine")
            || !g_strcmp0 (hlsdemux->drm_type, "clearkey"))
        && hlsdemux->drm_systemid && hls_stream->current_protection_meta) {
      if (g_strcmp0 (hls_stream->protection_meta_cache,
              hls_stream->current_protection_meta) != 0) {
        GstEvent *event;
        GstBuffer *pssi;
        glong pssi_len;

        g_free (hls_stream->protection_meta_cache);

        hls_stream->protection_meta_cache =
            g_strdup (hls_stream->current_protection_meta);
        pssi_len = strlen (hls_stream->protection_meta_cache);
        pssi =
            gst_buffer_new_wrapped (g_memdup (hls_stream->protection_meta_cache,
                pssi_len), pssi_len);
        event =
            gst_event_new_protection (hlsdemux->drm_systemid, pssi,
            "hls-streaming");
        GST_LOG_OBJECT (stream, "Queuing Protection event on source pad %s",
            hls_stream->protection_meta_cache);
        gst_adaptive_demux_stream_queue_event (stream, event);
        gst_buffer_unref (pssi);
      }
    }
    return TRUE;
  }

  m3u8 = gst_hls_demux_stream_get_m3u8 (hls_stream);

  if (hlsdemux->drm_clientid && !g_strcmp0 (hlsdemux->drm_type, "verimatrix"))
    if (hlsdemux->drm_ctrl_handle)
      key =
          gst_hls_demux_get_key_from_drm_service (hlsdemux,
          hls_stream->current_key);
    else
      key = NULL;
  else
    key = gst_hls_demux_get_key (hlsdemux, hls_stream->current_key,
        demux->referer, m3u8->allowcache);

  if (key == NULL)
    goto key_failed;

  gst_hls_demux_stream_decrypt_start (hls_stream, key->data,
      hls_stream->current_iv);

  return TRUE;

key_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
        ("Couldn't retrieve key for decryption"), (NULL));
    GST_WARNING_OBJECT (demux, "Failed to decrypt data");
    return FALSE;
  }
}

static GstHLSTSReaderType
caps_to_reader (const GstCaps * caps)
{
  const GstStructure *s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "video/mpegts"))
    return GST_HLS_TSREADER_MPEGTS;
  if (gst_structure_has_name (s, "application/x-id3"))
    return GST_HLS_TSREADER_ID3;
  if (gst_structure_has_name (s, "video/quicktime"))
    return GST_HLS_TSREADER_FMP4;
  if (gst_structure_has_name (s, "application/x-subtitle-vtt"))
    return GST_HLS_TSREADER_WEBVTT;

  return GST_HLS_TSREADER_NONE;
}

/* This code is imported from dashdemux's isobmff buffer parsing function */
static GstBuffer *
_gst_buffer_split (GstBuffer * buffer, gint offset, gsize size)
{
  GstBuffer *newbuf = gst_buffer_copy_region (buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META
      | GST_BUFFER_COPY_MEMORY, offset, size == -1 ? size : size - offset);

  gst_buffer_resize (buffer, 0, offset);

  return newbuf;
}

static GstBuffer *
gst_hls_demux_parse_isobmff (GstAdaptiveDemux * demux,
    GstHLSDemuxStream * hls_stream, GstBuffer * buffer)
{
  GstAdaptiveDemuxStream *stream = (GstAdaptiveDemuxStream *) hls_stream;
  gsize available;
  GstMapInfo map;
  GstByteReader reader;
  guint32 fourcc;
  guint header_size;
  guint64 size, buffer_offset;

  g_assert (hls_stream->isobmff_parser.current_fourcc != GST_ISOFF_FOURCC_MDAT);

  if (hls_stream->isobmff_parser.current_offset == -1) {
    hls_stream->isobmff_parser.current_offset =
        GST_BUFFER_OFFSET_IS_VALID (buffer) ? GST_BUFFER_OFFSET (buffer) : 0;
  }

  gst_adapter_push (hls_stream->isobmff_adapter, buffer);

  available = gst_adapter_available (hls_stream->isobmff_adapter);
  buffer = gst_adapter_take_buffer (hls_stream->isobmff_adapter, available);
  buffer_offset = hls_stream->isobmff_parser.current_offset;

  /* Always at the start of a box here */
  g_assert (hls_stream->isobmff_parser.current_size == 0);

  /* At the start of a box => Parse it */
  gst_buffer_map (buffer, &map, GST_MAP_READ);
  gst_byte_reader_init (&reader, map.data, map.size);

  /* While there are more boxes left to parse ... */
  hls_stream->isobmff_parser.current_start_offset =
      hls_stream->isobmff_parser.current_offset;
  do {
    hls_stream->isobmff_parser.current_fourcc = 0;
    hls_stream->isobmff_parser.current_size = 0;

    if (!gst_isoff_parse_box_header (&reader, &fourcc, NULL, &header_size,
            &size)) {
      break;
    }

    hls_stream->isobmff_parser.current_fourcc = fourcc;
    if (size == 0) {
      /* We assume this is mdat, anything else with "size until end"
       * does not seem to make sense */
      g_assert (hls_stream->isobmff_parser.current_fourcc ==
          GST_ISOFF_FOURCC_MDAT);
      hls_stream->isobmff_parser.current_size = -1;
      break;
    }

    hls_stream->isobmff_parser.current_size = size;

    /* Do we have the complete box or are at MDAT */
    if (gst_byte_reader_get_remaining (&reader) < size - header_size ||
        hls_stream->isobmff_parser.current_fourcc == GST_ISOFF_FOURCC_MDAT) {
      /* Reset byte reader to the beginning of the box */
      gst_byte_reader_set_pos (&reader,
          gst_byte_reader_get_pos (&reader) - header_size);
      break;
    }

    GST_LOG_OBJECT (stream->pad,
        "box %" GST_FOURCC_FORMAT " at offset %" G_GUINT64_FORMAT " size %"
        G_GUINT64_FORMAT, GST_FOURCC_ARGS (fourcc),
        hls_stream->isobmff_parser.current_offset +
        gst_byte_reader_get_pos (&reader) - header_size, size);

    if (hls_stream->isobmff_parser.current_fourcc == GST_ISOFF_FOURCC_MOOF) {
      GstByteReader sub_reader;

      g_assert (hls_stream->moof == NULL);
      gst_byte_reader_get_sub_reader (&reader, &sub_reader, size - header_size);
      hls_stream->moof = gst_isoff_moof_box_parse (&sub_reader);

    } else if (hls_stream->isobmff_parser.current_fourcc ==
        GST_ISOFF_FOURCC_MOOV) {
      GstByteReader sub_reader;

      gst_byte_reader_get_sub_reader (&reader, &sub_reader, size - header_size);
      if (hls_stream->moov)
        gst_isoff_moov_box_free (hls_stream->moov);
      hls_stream->moov = gst_isoff_moov_box_parse (&sub_reader);

    } else {
      gst_byte_reader_skip (&reader, size - header_size);
    }

    hls_stream->isobmff_parser.current_fourcc = 0;
    hls_stream->isobmff_parser.current_start_offset += size;
    hls_stream->isobmff_parser.current_size = 0;
  } while (gst_byte_reader_get_remaining (&reader) > 0);

  gst_buffer_unmap (buffer, &map);

  /* mdat? Push all we have and wait for it to be over */
  if (hls_stream->isobmff_parser.current_fourcc == GST_ISOFF_FOURCC_MDAT) {
    GstBuffer *pending;

    GST_LOG_OBJECT (stream->pad,
        "box %" GST_FOURCC_FORMAT " at offset %" G_GUINT64_FORMAT " size %"
        G_GUINT64_FORMAT, GST_FOURCC_ARGS (fourcc),
        hls_stream->isobmff_parser.current_offset +
        gst_byte_reader_get_pos (&reader) - header_size,
        hls_stream->isobmff_parser.current_size);

    /* At mdat. Move the start of the mdat to the adapter and have everything
     * else be pushed. We parsed all header boxes at this point and are not
     * supposed to be called again until the next moof */
    pending = _gst_buffer_split (buffer, gst_byte_reader_get_pos (&reader), -1);
    gst_adapter_push (hls_stream->isobmff_adapter, pending);
    hls_stream->isobmff_parser.current_offset +=
        gst_byte_reader_get_pos (&reader);
    hls_stream->isobmff_parser.current_size = 0;

    GST_BUFFER_OFFSET (buffer) = buffer_offset;
    GST_BUFFER_OFFSET_END (buffer) =
        buffer_offset + gst_buffer_get_size (buffer);
    return buffer;
  } else if (gst_byte_reader_get_pos (&reader) != 0) {
    GstBuffer *pending;

    /* Multiple complete boxes and no mdat? Push them and keep the remainder,
     * which is the start of the next box if any remainder */

    pending = _gst_buffer_split (buffer, gst_byte_reader_get_pos (&reader), -1);
    gst_adapter_push (hls_stream->isobmff_adapter, pending);
    hls_stream->isobmff_parser.current_offset +=
        gst_byte_reader_get_pos (&reader);
    hls_stream->isobmff_parser.current_size = 0;

    GST_BUFFER_OFFSET (buffer) = buffer_offset;
    GST_BUFFER_OFFSET_END (buffer) =
        buffer_offset + gst_buffer_get_size (buffer);
    return buffer;
  }

  /* Not even a single complete, non-mdat box, wait */
  hls_stream->isobmff_parser.current_size = 0;
  gst_adapter_push (hls_stream->isobmff_adapter, buffer);

  return NULL;
}

static GstBuffer *
gst_hls_demux_handle_isobmff_buffer (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  if (buffer == NULL)
    return NULL;

  if (hls_stream->isobmff_parser.current_fourcc != GST_ISOFF_FOURCC_MDAT) {
    buffer = gst_hls_demux_parse_isobmff (demux, hls_stream, buffer);

    if (hls_stream->find_presentation_offset &&
        hls_stream->isobmff_parser.current_fourcc != GST_ISOFF_FOURCC_MDAT) {
      /* Cannot push data until get moof, and until figure out the first pts */
      if (hls_stream->pending_pts_buffer) {
        if (buffer) {
          hls_stream->pending_pts_buffer =
              gst_buffer_append (hls_stream->pending_pts_buffer, buffer);
        }
      } else {
        hls_stream->pending_pts_buffer = buffer;
      }
      return NULL;
    }
  } else if (gst_adapter_available (hls_stream->isobmff_adapter) > 0) {
    gst_adapter_push (hls_stream->isobmff_adapter, buffer);

    buffer =
        gst_adapter_take_buffer (hls_stream->isobmff_adapter,
        gst_adapter_available (hls_stream->isobmff_adapter));
  }

  if (G_UNLIKELY (hls_stream->pending_pts_buffer)) {
    if (buffer) {
      hls_stream->pending_pts_buffer =
          gst_buffer_append (hls_stream->pending_pts_buffer, buffer);
      buffer = hls_stream->pending_pts_buffer;
    } else {
      buffer = hls_stream->pending_pts_buffer;
    }
    hls_stream->pending_pts_buffer = NULL;
  }

  if (G_UNLIKELY (hls_stream->find_presentation_offset) && hls_stream->moov &&
      hls_stream->moof && buffer) {
    GstClockTime min_pts =
        gst_isoff_get_min_pts (hls_stream->moov, hls_stream->moof);
    GstStreamType stream_type = gst_stream_get_stream_type (stream->object);
    if (min_pts != GST_CLOCK_TIME_NONE) {
      GstSegment segment;
      GST_DEBUG_OBJECT (stream->pad, "Adjust segment %" GST_PTR_FORMAT
          " based on min pts %" GST_TIME_FORMAT ", buffer pts = %"
          GST_TIME_FORMAT, stream->pending_segment,
          GST_TIME_ARGS (min_pts), GST_TIME_ARGS (stream->fragment.timestamp));

      hls_stream->presentation_offset = min_pts - stream->fragment.timestamp;
      if (stream_type == GST_STREAM_TYPE_TEXT) {
        gst_element_post_message (GST_ELEMENT_CAST (demux),
            gst_message_new_element (GST_OBJECT_CAST (demux),
                gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                    "min-pts", G_TYPE_UINT64, min_pts,
                    "buffer-pts", G_TYPE_UINT64, stream->fragment.timestamp,
                    NULL)));
      }

      gst_segment_copy_into (&demux->segment, &segment);
      if (segment.rate > 0) {
        GstEvent *event;
        guint32 seqnum = gst_event_get_seqnum (stream->pending_segment);
        segment.start = min_pts;
        segment.position = min_pts;
        segment.time = stream->fragment.timestamp;
        segment.base = gst_segment_to_running_time (&demux->segment,
            GST_FORMAT_TIME, stream->fragment.timestamp);

        if (hls_stream->start_offset_after_seek > 0) {
          segment.start += hls_stream->start_offset_after_seek;
          segment.time += hls_stream->start_offset_after_seek;
          hls_stream->start_offset_after_seek = 0;
        }

        event = gst_event_new_segment (&segment);
        gst_event_set_seqnum (event, seqnum);
        gst_event_replace (&stream->pending_segment, event);
        gst_event_unref (event);

        if (stream->pending_stream_start == NULL) {
          GstEvent *event;
          const gchar *stream_id = gst_stream_get_stream_id (stream->object);
          gchar *seq = g_strdup_printf ("_%" G_GUINT64_FORMAT,
              hls_stream->playlist->sequence);
          gchar *new_stream_id = g_strconcat (stream_id, seq, NULL);

          event =
              gst_event_new_stream_start (stream_type ==
              GST_STREAM_TYPE_TEXT ? stream_id : new_stream_id);
          gst_event_set_stream_flags (event,
              gst_stream_get_stream_flags (stream->object));
          gst_event_replace (&stream->pending_stream_start, event);
          gst_event_unref (event);
          g_free (new_stream_id);
          g_free (seq);
        }
      } else {
        /* FIXME: how to handle negative rate ? */
      }
    }
    hls_stream->find_presentation_offset = FALSE;
  }

  return buffer;
}

static gchar *
gst_hls_parse_webvtt_line (char *source, char *dest)
{
  gchar *p = source;
  gint64 length = 0;

  while (!(*p == '\r' || *p == '\n' || *p == '\0')) {
    p++;
    length++;
  }

  strncpy (dest, source, length);

  if (*p == '\r' && *(p + 1) == '\n') {
    p += 2;
  } else if (*p == '\n' && *(p + 1) == '\r') {
    p += 2;
  } else if (*p == '\n' || *p == '\r') {
    p++;
  }

  if (*p) {
    return p;
  } else {
    return NULL;
  }
}

static gboolean
gst_hls_demux_webvtt_read_x_timestamp_map (gchar * data, guint64 * local,
    guint64 * mpegts)
{
  guint64 ts;
  guint hour, min, sec, msec;

  if (sscanf (data,
          "X-TIMESTAMP-MAP=MPEGTS:%" G_GUINT64_FORMAT ",LOCAL:%u:%u:%u.%u",
          &ts, &hour, &min, &sec, &msec) != 5) {
    if (sscanf (data,
            "X-TIMESTAMP-MAP=LOCAL:%u:%u:%u.%u,MPEGTS:%" G_GUINT64_FORMAT,
            &hour, &min, &sec, &msec, &ts) != 5) {
      return FALSE;
    }
  }

  *local = ((hour * 3600) + (min * 60) + sec) * GST_SECOND + msec * GST_MSECOND;
  *mpegts = (((ts) * (guint64) 100000) / 9);

  GST_DEBUG ("local time:%" GST_TIME_FORMAT ", mpegts time:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (*local), GST_TIME_ARGS (*mpegts));

  return TRUE;
}

#define X_TIMESTAMP_MAP_DATA_LENGTH 100

static gboolean
gst_hls_demux_parse_webvtt (GstBuffer * buffer, guint64 * local,
    guint64 * mpegts)
{
  gchar *ptr;
  gchar *timestamp_map_ptr;
  gboolean have_timestamp_map = FALSE;

  // using playlist parser
  ptr = gst_hls_src_buf_to_utf8_playlist (buffer);
  timestamp_map_ptr = ptr;

  while (timestamp_map_ptr) {
    gchar data[X_TIMESTAMP_MAP_DATA_LENGTH] = { 0, };
    timestamp_map_ptr = gst_hls_parse_webvtt_line (timestamp_map_ptr, data);

    if (g_str_has_prefix (data, "X-TIMESTAMP-MAP=")) {
      have_timestamp_map = TRUE;
      if (!gst_hls_demux_webvtt_read_x_timestamp_map (data, local, mpegts)) {
        GST_WARNING ("failed to parse x-timestamp-map string '%s'", data);
        g_free (ptr);
        return FALSE;
      }
      break;
    }
  }

  if (!have_timestamp_map) {
    GST_WARNING ("Don't have X-TIMESTAMP-MAP");
    g_free (ptr);
    return FALSE;
  }

  g_free (ptr);
  return TRUE;
}

static gboolean
gst_hls_demux_handle_webvtt_buffer (GstBuffer * buffer, guint64 * local,
    guint64 * mpegts)
{
  gboolean ret = FALSE;

  if (buffer == NULL)
    return ret;

  ret = gst_hls_demux_parse_webvtt (buffer, local, mpegts);

  return ret;
}

static GstFlowReturn
gst_hls_demux_handle_buffer (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer, gboolean at_eos)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  //GstClockTime first_pcr, last_pcr;
  //GstTagList *tags;

  if (buffer == NULL)
    return GST_FLOW_OK;

  if (G_UNLIKELY (hls_stream->do_typefind)) {
    GstCaps *caps = NULL;
    guint buffer_size;
    GstTypeFindProbability prob = GST_TYPE_FIND_NONE;
    GstMapInfo info;

    if (hls_stream->pending_typefind_buffer)
      buffer = gst_buffer_append (hls_stream->pending_typefind_buffer, buffer);
    hls_stream->pending_typefind_buffer = NULL;

    gst_buffer_map (buffer, &info, GST_MAP_READ);
    buffer_size = info.size;

    /* Typefind could miss if buffer is too small. In this case we
     * will retry later */
    if (buffer_size >= (2 * 1024) || at_eos) {
      caps =
          gst_type_find_helper_for_data (GST_OBJECT_CAST (hlsdemux), info.data,
          info.size, &prob);
    }

    if (G_UNLIKELY (!caps)) {
      /* Won't need this mapping any more all paths return inside this if() */
      gst_buffer_unmap (buffer, &info);

      /* Only fail typefinding if we already a good amount of data
       * and we still don't know the type */
      if (buffer_size > (2 * 1024 * 1024) || at_eos) {
        GST_ELEMENT_ERROR (hlsdemux, STREAM, TYPE_NOT_FOUND,
            ("Could not determine type of stream"), (NULL));
        gst_buffer_unref (buffer);
        return GST_FLOW_NOT_NEGOTIATED;
      }

      hls_stream->pending_typefind_buffer = buffer;

      return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT (hlsdemux, "Typefind result: %" GST_PTR_FORMAT " prob:%d",
        caps, prob);

    hls_stream->stream_type = caps_to_reader (caps);
    gst_hlsdemux_tsreader_set_type (&hls_stream->tsreader,
        hls_stream->stream_type);

    gst_hls_demux_set_stream_type (hlsdemux, hls_stream, caps);
    gst_adaptive_demux_stream_set_caps (stream, caps);
    gst_caps_unref (caps);

    if (hls_stream->stream_type == GST_HLS_TSREADER_FMP4) {
      hls_stream->isobmff_adapter = gst_adapter_new ();
      hls_stream->find_presentation_offset = TRUE;
    }

    if (hls_stream->stream_type == GST_HLS_TSREADER_WEBVTT) {
      gst_element_post_message (GST_ELEMENT_CAST (hlsdemux),
          gst_message_new_element (GST_OBJECT_CAST (hlsdemux),
              gst_structure_new ("webvtt",
                  "is-webvtt", G_TYPE_BOOLEAN, TRUE, NULL)));

      hls_stream->find_presentation_offset = TRUE;
    }

    if (hls_stream->stream_type == GST_HLS_TSREADER_MPEGTS) {
      hls_stream->find_presentation_offset = TRUE;
    }

    hls_stream->do_typefind = FALSE;

    gst_buffer_unmap (buffer, &info);
  }
  g_assert (hls_stream->pending_typefind_buffer == NULL);

  // Accumulate this buffer
  if (hls_stream->pending_pcr_buffer) {
    buffer = gst_buffer_append (hls_stream->pending_pcr_buffer, buffer);
    hls_stream->pending_pcr_buffer = NULL;
  }

  if (hls_stream->stream_type == GST_HLS_TSREADER_FMP4) {
    buffer = gst_hls_demux_handle_isobmff_buffer (demux, stream, buffer);
    if (!buffer)
      return GST_FLOW_OK;
  }

  if (hls_stream->stream_type == GST_HLS_TSREADER_WEBVTT) {
    if (hls_stream->find_presentation_offset) {
      guint64 local = 0;
      guint64 mpegts = 0;

      if (!gst_hls_demux_handle_webvtt_buffer (buffer, &local, &mpegts)) {
        GST_WARNING ("failed to handle webvtt buffer");
      } else {
        gst_element_post_message (GST_ELEMENT_CAST (demux),
            gst_message_new_element (GST_OBJECT_CAST (demux),
                gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                    "local-time", G_TYPE_UINT64, local,
                    "mpegts-time", G_TYPE_UINT64, mpegts, NULL)));
      }
      hls_stream->find_presentation_offset = FALSE;
    }
  }

  if (hls_stream->stream_type == GST_HLS_TSREADER_MPEGTS) {
    if (hls_stream->find_presentation_offset) {
      //retrieve video raw pts
      GstClockTime first_pcr, last_pcr;
      GstTagList *tags;

      if (!gst_hlsdemux_tsreader_find_pcrs (&hls_stream->tsreader, &buffer,
              &first_pcr, &last_pcr, &tags)) {
        GST_WARNING_OBJECT (hlsdemux, "Cannot retreive pts");
      } else {
        GST_DEBUG_OBJECT (hlsdemux,
            "first_pcr: %" GST_TIME_FORMAT " last_pcr: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (first_pcr), GST_TIME_ARGS (last_pcr));

        //first pcr message
        gst_element_post_message (GST_ELEMENT_CAST (hlsdemux),
            gst_message_new_element (GST_OBJECT_CAST (hlsdemux),
                gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                    "min-pts", G_TYPE_UINT64, first_pcr,
                    "buffer-pts", G_TYPE_UINT64, stream->fragment.timestamp,
                    NULL)));
      }
      hls_stream->find_presentation_offset = FALSE;
    }
  }
#if 0
  else if (!gst_hlsdemux_tsreader_find_pcrs (&hls_stream->tsreader, &buffer,
          &first_pcr, &last_pcr, &tags)
      && !at_eos) {
    // Store this buffer for later
    hls_stream->pending_pcr_buffer = buffer;
    return GST_FLOW_OK;
  }

  if (tags) {
    gst_adaptive_demux_stream_set_tags (stream, tags);
    /* run typefind again on the trimmed buffer */
    hls_stream->do_typefind = TRUE;
    return gst_hls_demux_handle_buffer (demux, stream, buffer, at_eos);
  }
#endif

  if (buffer) {
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_OFFSET (buffer) = hls_stream->current_offset;
    hls_stream->current_offset += gst_buffer_get_size (buffer);
    GST_BUFFER_OFFSET_END (buffer) = hls_stream->current_offset;
    return gst_adaptive_demux_stream_push_buffer (stream, buffer);
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_finish_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstFlowReturn ret = GST_FLOW_OK;

  if (hls_stream->current_key)
    gst_hls_demux_stream_decrypt_end (hls_stream);

  if (stream->last_ret == GST_FLOW_OK) {
    if (hls_stream->pending_decrypted_buffer) {
      if (hls_stream->current_key) {
        GstMapInfo info;
        gssize unpadded_size;

        /* Handle pkcs7 unpadding here */
        gst_buffer_map (hls_stream->pending_decrypted_buffer, &info,
            GST_MAP_READ);
        unpadded_size = info.size - info.data[info.size - 1];
        gst_buffer_unmap (hls_stream->pending_decrypted_buffer, &info);

        gst_buffer_resize (hls_stream->pending_decrypted_buffer, 0,
            unpadded_size);
      }

      ret =
          gst_hls_demux_handle_buffer (demux, stream,
          hls_stream->pending_decrypted_buffer, TRUE);
      hls_stream->pending_decrypted_buffer = NULL;
    }

    if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED) {
      if (G_UNLIKELY (hls_stream->pending_typefind_buffer)) {
        GstBuffer *buf = hls_stream->pending_typefind_buffer;
        hls_stream->pending_typefind_buffer = NULL;

        gst_hls_demux_handle_buffer (demux, stream, buf, TRUE);
      }

      if (hls_stream->pending_pcr_buffer) {
        GstBuffer *buf = hls_stream->pending_pcr_buffer;
        hls_stream->pending_pcr_buffer = NULL;

        ret = gst_hls_demux_handle_buffer (demux, stream, buf, TRUE);
      }

      if (GST_IS_ADAPTER (hls_stream->isobmff_adapter)
          && G_UNLIKELY (gst_adapter_available (hls_stream->isobmff_adapter) >
              0)) {
        GstBuffer *buf = NULL;
        buf =
            gst_adapter_take_buffer (hls_stream->isobmff_adapter,
            gst_adapter_available (hls_stream->isobmff_adapter));

        ret = gst_hls_demux_handle_buffer (demux, stream, buf, TRUE);
      }

      GST_LOG_OBJECT (stream,
          "Fragment PCRs were %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (hls_stream->tsreader.first_pcr),
          GST_TIME_ARGS (hls_stream->tsreader.last_pcr));
    }
  }

  if (G_UNLIKELY (stream->downloading_header || stream->downloading_index))
    return GST_FLOW_OK;

  gst_hls_demux_stream_clear_pending_data (hls_stream);

  if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED)
    return gst_adaptive_demux_stream_advance_fragment (demux, stream,
        stream->fragment.duration);
  return ret;
}

static GstClockTime
gst_hls_demux_get_presentation_offset (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  return hls_stream->presentation_offset;
}

static GstFlowReturn
gst_hls_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstBuffer * buffer)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  if (hls_stream->current_offset == -1)
    hls_stream->current_offset = 0;

  /* Is it encrypted? */
  if (hls_stream->current_key) {
    GError *err = NULL;
    gsize size;
    GstBuffer *tmp_buffer;

    if (hls_stream->pending_encrypted_data == NULL)
      hls_stream->pending_encrypted_data = gst_adapter_new ();

    gst_adapter_push (hls_stream->pending_encrypted_data, buffer);
    size = gst_adapter_available (hls_stream->pending_encrypted_data);

    /* must be a multiple of 16 */
    size &= (~0xF);

    if (size == 0) {
      return GST_FLOW_OK;
    }

    buffer = gst_adapter_take_buffer (hls_stream->pending_encrypted_data, size);
    buffer =
        gst_hls_demux_decrypt_fragment (hlsdemux, hls_stream, buffer, &err);
    if (buffer == NULL) {
      GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Failed to decrypt buffer"),
          ("decryption failed %s", err->message));
      g_error_free (err);
      return GST_FLOW_ERROR;
    }

    tmp_buffer = hls_stream->pending_decrypted_buffer;
    hls_stream->pending_decrypted_buffer = buffer;
    buffer = tmp_buffer;
  }

  return gst_hls_demux_handle_buffer (demux, stream, buffer, FALSE);
}

static void
gst_hls_demux_stream_free (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  if (hls_stream->playlist) {
    gst_m3u8_unref (hls_stream->playlist);
    hls_stream->playlist = NULL;
  }

  if (hls_stream->media) {
    gst_hls_media_unref (hls_stream->media);
    hls_stream->media = NULL;
  }

  if (hls_stream->pending_encrypted_data)
    g_object_unref (hls_stream->pending_encrypted_data);

  gst_buffer_replace (&hls_stream->pending_decrypted_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_typefind_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_pcr_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_pts_buffer, NULL);

  if (hls_stream->current_key) {
    g_free (hls_stream->current_key);
    hls_stream->current_key = NULL;
  }
  if (hls_stream->current_iv) {
    g_free (hls_stream->current_iv);
    hls_stream->current_iv = NULL;
  }
  if (hls_stream->protection_meta_cache) {
    g_free (hls_stream->protection_meta_cache);
    hls_stream->protection_meta_cache = NULL;
  }
  if (hls_stream->current_protection_meta) {
    g_free (hls_stream->current_protection_meta);
    hls_stream->current_protection_meta = NULL;
  }
  gst_hls_demux_stream_decrypt_end (hls_stream);

  if (hls_stream->isobmff_adapter)
    g_object_unref (hls_stream->isobmff_adapter);
  if (hls_stream->moof)
    gst_isoff_moof_box_free (hls_stream->moof);
  if (hls_stream->moov)
    gst_isoff_moov_box_free (hls_stream->moov);
}

static GstM3U8 *
gst_hls_demux_stream_get_m3u8 (GstHLSDemuxStream * hlsdemux_stream)
{
  GstM3U8 *m3u8;

  m3u8 = hlsdemux_stream->playlist;

  return m3u8;
}

static void
gst_hls_demux_stream_set_m3u8 (GstHLSDemuxStream * hlsdemux_stream,
    GstM3U8 * m3u8)
{
  hlsdemux_stream->playlist = m3u8;
  hlsdemux_stream->rendition_switched = TRUE;
}

static gboolean
gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemuxStream * stream)
{
  gboolean has_next;
  GstM3U8 *m3u8;

  m3u8 = gst_hls_demux_stream_get_m3u8 (GST_HLS_DEMUX_STREAM_CAST (stream));

  has_next = gst_m3u8_has_next_fragment (m3u8, stream->demux->segment.rate > 0);

  return has_next;
}

static GstFlowReturn
gst_hls_demux_advance_fragment (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstM3U8 *m3u8;
  gboolean restart = FALSE;

  m3u8 = gst_hls_demux_stream_get_m3u8 (hlsdemux_stream);

  if (!gst_m3u8_advance_fragment (m3u8, stream->demux->segment.rate > 0)) {
    if (gst_m3u8_is_live (m3u8)) {
      GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
      guint num_of_segments = g_list_length (m3u8->files);
      restart = TRUE;
      GST_INFO ("restart-live num-of-segments: %u", num_of_segments);
      gst_element_post_message (GST_ELEMENT_CAST (hlsdemux),
          gst_message_new_element (GST_OBJECT_CAST (hlsdemux),
              gst_structure_new ("restart-live",
                  "num-of-segments", G_TYPE_UINT, num_of_segments, NULL)));
    }
  }

  hlsdemux_stream->reset_pts = FALSE;

  if (hlsdemux_stream->isobmff_adapter)
    gst_adapter_clear (hlsdemux_stream->isobmff_adapter);
  hlsdemux_stream->isobmff_parser.current_fourcc = 0;
  hlsdemux_stream->isobmff_parser.current_start_offset = 0;
  hlsdemux_stream->isobmff_parser.current_offset = 0;
  hlsdemux_stream->isobmff_parser.current_size = 0;

  if (hlsdemux_stream->moof)
    gst_isoff_moof_box_free (hlsdemux_stream->moof);
  hlsdemux_stream->moof = NULL;

  return restart ? GST_FLOW_EOS : GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_update_fragment_info (GstAdaptiveDemuxStream * stream)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  GstM3U8MediaFile *file;
  GstClockTime sequence_pos;
  gboolean discont, forward;
  GstM3U8 *m3u8;
  GstStreamType stream_type = gst_stream_get_stream_type (stream->object);

  m3u8 = gst_hls_demux_stream_get_m3u8 (hlsdemux_stream);

  forward = (stream->demux->segment.rate > 0);
  file = gst_m3u8_get_next_fragment (m3u8, forward, &sequence_pos, &discont);

  if (file == NULL) {
    GST_INFO_OBJECT (hlsdemux, "This playlist doesn't contain more fragments");
    return GST_FLOW_EOS;
  } else if (stream_type == GST_STREAM_TYPE_VIDEO
      || stream_type == GST_STREAM_TYPE_CONTAINER) {
    guint64 segment_duration = file->duration;
    gst_element_post_message (GST_ELEMENT_CAST (hlsdemux),
        gst_message_new_element (GST_OBJECT_CAST (hlsdemux),
            gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                "segment-duration", G_TYPE_UINT64,
                GST_TIME_AS_SECONDS (segment_duration), NULL)));
  }

  if (gst_stream_get_stream_type (stream->object) == GST_STREAM_TYPE_AUDIO) {
    if (hlsdemux_stream->rendition_switched) {
      GST_DEBUG_OBJECT (stream->pad, "Audio rendition switched.");
      discont = TRUE;
      hlsdemux_stream->rendition_switched = FALSE;
    }
  }

  if (discont) {
    stream->need_header = TRUE;
    hlsdemux_stream->find_presentation_offset = TRUE;
  }

  /* FIXME: We will ignore EXT-X-MAP tag information in playlist with version
   * less than 5. [QEVENTSEVT-25604] */
  if (m3u8->version >= 5 &&
      GST_ADAPTIVE_DEMUX_STREAM_NEED_HEADER (stream) && file->init_file) {
    GstM3U8InitFile *header_file = file->init_file;
    g_free (stream->fragment.header_uri);
    stream->fragment.header_uri = g_strdup (header_file->uri);
    stream->fragment.header_range_start = header_file->offset;
    if (header_file->size != -1) {
      stream->fragment.header_range_end =
          header_file->offset + header_file->size - 1;
    } else {
      stream->fragment.header_range_end = -1;
    }
    if (hlsdemux_stream->moov) {
      gst_isoff_moov_box_free (hlsdemux_stream->moov);
      hlsdemux_stream->moov = NULL;
    }
  }

  if (stream->discont)
    discont = TRUE;

  /* set up our source for download */
  if (hlsdemux_stream->reset_pts || discont
      || stream->demux->segment.rate < 0.0) {
    stream->fragment.timestamp = sequence_pos;
  } else {
    stream->fragment.timestamp = GST_CLOCK_TIME_NONE;
  }

  g_free (hlsdemux_stream->current_key);
  hlsdemux_stream->current_key = g_strdup (file->key);
  g_free (hlsdemux_stream->current_iv);
  hlsdemux_stream->current_iv = g_memdup (file->iv, sizeof (file->iv));
  g_free (hlsdemux_stream->current_protection_meta);
  hlsdemux_stream->current_protection_meta = g_strdup (file->protection_meta);
  g_free (stream->fragment.uri);
  stream->fragment.uri = g_strdup (file->uri);

  GST_DEBUG_OBJECT (hlsdemux, "Stream %p URI now %s", stream, file->uri);

  stream->fragment.range_start = file->offset;
  if (file->size != -1)
    stream->fragment.range_end = file->offset + file->size - 1;
  else
    stream->fragment.range_end = -1;

  stream->fragment.duration = file->duration;

  if (discont)
    stream->discont = TRUE;

  gst_m3u8_media_file_unref (file);

  return GST_FLOW_OK;
}

static gboolean
gst_hls_demux_select_bitrate (GstAdaptiveDemuxStream * stream, guint64 bitrate)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (stream->demux);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);

  gboolean changed = FALSE;

  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  if (hlsdemux->master == NULL || hlsdemux->master->is_simple) {
    GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
    return FALSE;
  }
  GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);

  if (stream->is_static) {
    GST_LOG_OBJECT (hlsdemux,
        "Stream %p Not choosing new bitrate - not the primary stream", stream);
    return FALSE;
  }

  gst_hls_demux_change_playlist (hlsdemux, bitrate / MAX (1.0,
          ABS (demux->segment.rate)), &changed);
  if (changed)
    gst_hls_demux_setup_streams (GST_ADAPTIVE_DEMUX_CAST (hlsdemux), TRUE);
  return changed;
}

static void
gst_hls_demux_reset (GstAdaptiveDemux * ademux)
{
  GstHLSDemux *demux = GST_HLS_DEMUX_CAST (ademux);
  GList *walk;

  GST_DEBUG_OBJECT (demux, "resetting");

  GST_M3U8_CLIENT_LOCK (hlsdemux->client);
  if (demux->master) {
    gst_hls_master_playlist_unref (demux->master);
    demux->master = NULL;
  }
  if (demux->current_variant != NULL) {
    gst_hls_variant_stream_unref (demux->current_variant);
    demux->current_variant = NULL;
  }

  if (!ademux->soft_flush)
    demux->srcpad_counter = 0;

  for (walk = ademux->streams; walk != NULL; walk = walk->next) {
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (walk->data);
    hls_stream->presentation_offset = 0;
  }

  gst_hls_demux_clear_all_pending_data (demux, TRUE);
  GST_M3U8_CLIENT_UNLOCK (hlsdemux->client);
}

static gchar *
gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf)
{
  GstMapInfo info;
  gchar *playlist;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    goto map_error;

  if (!g_utf8_validate ((gchar *) info.data, info.size, NULL))
    goto validate_error;

  /* alloc size + 1 to end with a null character */
  playlist = g_malloc0 (info.size + 1);
  memcpy (playlist, info.data, info.size);

  gst_buffer_unmap (buf, &info);
  return playlist;

validate_error:
  gst_buffer_unmap (buf, &info);
map_error:
  return NULL;
}

static gint
gst_hls_demux_find_variant_match (const GstHLSVariantStream * a,
    const GstHLSVariantStream * b)
{
  if (g_strcmp0 (a->name, b->name) == 0 &&
      a->bandwidth == b->bandwidth &&
      a->program_id == b->program_id &&
      g_strcmp0 (a->codecs, b->codecs) == 0 &&
      a->width == b->width &&
      a->height == b->height && a->iframe == b->iframe) {
    return 0;
  }

  return 1;
}

/* Update the master playlist, which contains the list of available
 * variants */
static gboolean
gst_hls_demux_update_variant_playlist (GstHLSDemux * hlsdemux, gchar * data,
    const gchar * uri, const gchar * base_uri)
{
  GstHLSMasterPlaylist *new_master, *old;
  gboolean ret = FALSE;
  GList *l, *unmatched_lists;
  GstHLSVariantStream *new_variant;

  new_master = gst_hls_master_playlist_new_from_data (data, base_uri ? base_uri : uri); // FIXME: check which uri to use here

  if (new_master == NULL)
    return ret;

  if (new_master->is_simple) {
    // FIXME: we should be able to support this though, in the unlikely
    // case that it changed?
    GST_ERROR
        ("Cannot update variant playlist: New playlist is not a variant playlist");
    gst_hls_master_playlist_unref (new_master);
    return FALSE;
  }

  GST_M3U8_CLIENT_LOCK (self);

  if (hlsdemux->master->is_simple) {
    GST_ERROR
        ("Cannot update variant playlist: Current playlist is not a variant playlist");
    gst_hls_master_playlist_unref (new_master);
    goto out;
  }

  /* Now see if the variant playlist still has the same lists */
  unmatched_lists = g_list_copy (hlsdemux->master->variants);
  for (l = new_master->variants; l != NULL; l = l->next) {
    GList *match = g_list_find_custom (unmatched_lists, l->data,
        (GCompareFunc) gst_hls_demux_find_variant_match);

    if (match) {
      GstHLSVariantStream *variant = l->data;
      GstHLSVariantStream *old = match->data;

      unmatched_lists = g_list_delete_link (unmatched_lists, match);
      /* FIXME: Deal with losing position due to missing an update */
      variant->m3u8->sequence_position = old->m3u8->sequence_position;
      variant->m3u8->sequence = old->m3u8->sequence;
    }
  }

  if (unmatched_lists != NULL) {
    GST_WARNING ("Unable to match all playlists");

    for (l = unmatched_lists; l != NULL; l = l->next) {
      if (l->data == hlsdemux->current_variant) {
        GST_WARNING ("Unable to match current playlist");
      }
    }

    g_list_free (unmatched_lists);
  }

  /* Switch out the variant playlist */
  old = hlsdemux->master;

  // FIXME: check all this and also switch of variants, if anything needs updating
  hlsdemux->master = new_master;

  if (hlsdemux->current_variant == NULL) {
    new_variant = new_master->default_variant;
  } else {
    /* Find the same variant in the new playlist */
    new_variant =
        gst_hls_master_playlist_get_matching_variant (new_master,
        hlsdemux->current_variant);
  }

  /* Use the function to set the current variant, as it copies over data */
  if (new_variant != NULL)
    gst_hls_demux_set_current_variant (hlsdemux, new_variant);

  gst_hls_master_playlist_unref (old);

  ret = (hlsdemux->current_variant != NULL);
out:
  GST_M3U8_CLIENT_UNLOCK (self);

  return ret;
}

static gboolean
gst_hls_demux_update_rendition_manifest (GstHLSDemux * demux,
    GstHLSMedia * media, GError ** err)
{
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX (demux);
  GstFragment *download;
  GstBuffer *buf;
  gchar *playlist;
  GstM3U8 *m3u8;
  gchar *uri = media->uri;

  download =
      gst_uri_downloader_fetch_uri (adaptive_demux->downloader, uri,
      adaptive_demux->referer, adaptive_demux->user_agent,
      adaptive_demux->cookies, TRUE, TRUE, TRUE, err);

  if (download == NULL)
    return FALSE;

  m3u8 = media->playlist;

  /* Set the base URI of the playlist to the redirect target if any */
  if (download->redirect_permanent && download->redirect_uri) {
    gst_m3u8_set_uri (m3u8, download->redirect_uri, NULL, media->name);
  } else {
    gst_m3u8_set_uri (m3u8, download->uri, download->redirect_uri, media->name);
  }

  buf = gst_fragment_get_buffer (download);
  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  gst_buffer_unref (buf);
  g_object_unref (download);

  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't validate playlist encoding");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
        "Couldn't validate playlist encoding");
    return FALSE;
  }

  if (!gst_m3u8_update (m3u8, playlist)) {
    if (gst_m3u8_is_live (m3u8)) {
      guint num_of_segments = g_list_length (m3u8->files);
      GST_WARNING_OBJECT (demux, "Couldn't update playlist");
      GST_INFO ("restart-live num-of-segments: %u", num_of_segments);
      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_element (GST_OBJECT_CAST (demux),
              gst_structure_new ("restart-live",
                  "num-of-segments", G_TYPE_UINT, num_of_segments, NULL)));
    }
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Couldn't update rendition playlist");
    return FALSE;
  }

  return TRUE;
}

#define ABSDIFF(x, y) ( (x) > (y) ? ((x) - (y)) : ((y) - (x)) )

static gboolean
gst_hls_demux_align_live_rendition_streams (GstHLSDemux * demux,
    GstM3U8 * media_playlist)
{
  GstM3U8 *m3u8;
  GList *walk;
  GstClockTime abs_diff;
  gint64 last_sequence, first_sequence;
  gboolean do_align = FALSE;

  g_assert (demux->current_variant != NULL);
  g_assert (demux->current_variant->m3u8 != NULL);

  m3u8 = demux->current_variant->m3u8;

  abs_diff =
      ABSDIFF (media_playlist->sequence_position, m3u8->sequence_position);
  last_sequence =
      GST_M3U8_MEDIA_FILE (g_list_last (media_playlist->files)->data)->sequence;
  first_sequence =
      GST_M3U8_MEDIA_FILE
      (g_list_first (media_playlist->files)->data)->sequence;

  GST_DEBUG_OBJECT (demux,
      "Rendition sequence:%" G_GINT64_FORMAT " , first_sequence:%"
      G_GINT64_FORMAT " , last_sequence:%" G_GINT64_FORMAT,
      media_playlist->sequence, first_sequence, last_sequence);

  /* Align renditions' timeline with that of variants */
  /* FIXME: HLS spec. is saying that alignment among streams should be done
   * by timestamp (not sequence number), but it's to hard to figure out timestamp in here ....
   */
  if (m3u8->sequence != media_playlist->sequence) {
    /* MEDIA-SEQUENCE among variant and rendition are different */
    GST_DEBUG_OBJECT (demux, "Sequence of rendition %" G_GINT64_FORMAT
        " is not aligned with variant %" G_GINT64_FORMAT,
        media_playlist->sequence, m3u8->sequence);

    if (m3u8->sequence > media_playlist->highest_sequence_number) {
      GST_WARNING_OBJECT (demux, "Rendition's highest_sequence_number %"
          G_GINT64_FORMAT " cannot follow variant's sequence %"
          G_GINT64_FORMAT, m3u8->sequence,
          media_playlist->highest_sequence_number);
      goto cannot_align;
    }

    do_align = TRUE;
  } else if (abs_diff > (media_playlist->targetduration / 2)) {
    /* m3u8's length are different */
    GST_DEBUG_OBJECT (demux, "Sequence position of rendition %" GST_TIME_FORMAT
        " is not aligned with variant %" GST_TIME_FORMAT,
        GST_TIME_ARGS (media_playlist->sequence_position),
        GST_TIME_ARGS (m3u8->sequence_position));
    do_align = TRUE;
  } else {
    GST_DEBUG_OBJECT (demux, "No need to align");
  }

  if (do_align) {
    walk = media_playlist->current_file;

    while (m3u8->sequence != media_playlist->sequence) {
      if (m3u8->sequence > media_playlist->sequence) {
        /* variant's sequence is faster than rendition, move toward last */
        walk = walk->next;
        media_playlist->sequence++;
      } else {
        walk = walk->prev;
        media_playlist->sequence--;
      }

      if (G_UNLIKELY (walk == NULL))
        goto cannot_align;
    }
    media_playlist->current_file = walk;
    media_playlist->sequence = m3u8->sequence;
    media_playlist->sequence_position = m3u8->sequence_position;

    media_playlist->last_file_end = m3u8->sequence_position;
    /* Re-calculate last_file_end */
    for (walk = media_playlist->current_file; walk; walk = g_list_next (walk)) {
      media_playlist->last_file_end +=
          GST_M3U8_MEDIA_FILE (walk->data)->duration;
    }

    if (media_playlist->last_file_end >= media_playlist->duration) {
      media_playlist->first_file_start =
          media_playlist->last_file_end - media_playlist->duration;
    } else {
      GST_FIXME_OBJECT (demux,
          "negative first_file_start, what should we do??");
      media_playlist->first_file_start = 0;
    }
  }

  return TRUE;

cannot_align:
  GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
      ("Cannot align timeline among streams"), (NULL));
  return FALSE;
}

static gboolean
gst_hls_demux_stream_update_playlist (GstHLSDemux * hlsdemux, GstM3U8 * m3u8)
{
  GList *walk;
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX (hlsdemux);

  for (walk = demux->streams; walk; walk = g_list_next (walk)) {
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (walk->data);
    GstM3U8 *old = gst_hls_demux_stream_get_m3u8 (hls_stream);

    if (old && old->uri && !strcmp (old->uri, m3u8->uri)) {
      GstAdaptiveDemuxStream *stream =
          GST_ADAPTIVE_DEMUX_STREAM_CAST (hls_stream);
      GST_DEBUG_OBJECT (stream->pad, "Found matching stream");
      gst_m3u8_unref (old);
      hls_stream->playlist = gst_m3u8_ref (m3u8);
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_hls_demux_update_playlist (GstHLSDemux * demux, gboolean update,
    GError ** err)
{
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX (demux);
  GstFragment *download;
  GstBuffer *buf;
  gchar *playlist;
  gboolean main_checked = FALSE;
  const gchar *main_uri;
  GstM3U8 *m3u8;
  gchar *uri;
  gint i;

retry:
  uri = gst_m3u8_get_uri (demux->current_variant->m3u8);
  main_uri = gst_adaptive_demux_get_manifest_ref_uri (adaptive_demux);
  download =
      gst_uri_downloader_fetch_uri (adaptive_demux->downloader, uri,
      adaptive_demux->referer, adaptive_demux->user_agent,
      adaptive_demux->cookies, TRUE, TRUE, TRUE, err);
  if (download == NULL) {
    gchar *base_uri;

    if (!update || main_checked || demux->master->is_simple
        || !gst_adaptive_demux_is_running (GST_ADAPTIVE_DEMUX_CAST (demux))) {
      g_free (uri);
      return FALSE;
    }
    g_clear_error (err);
    GST_INFO_OBJECT (demux,
        "Updating playlist %s failed, attempt to refresh variant playlist %s",
        uri, main_uri);
    download =
        gst_uri_downloader_fetch_uri (adaptive_demux->downloader,
        main_uri, adaptive_demux->referer, adaptive_demux->user_agent,
        adaptive_demux->cookies, TRUE, TRUE, TRUE, err);
    if (download == NULL) {
      g_free (uri);
      return FALSE;
    }

    buf = gst_fragment_get_buffer (download);
    playlist = gst_hls_src_buf_to_utf8_playlist (buf);
    gst_buffer_unref (buf);

    if (playlist == NULL) {
      GST_WARNING_OBJECT (demux,
          "Failed to validate variant playlist encoding");
      g_free (uri);
      g_object_unref (download);
      g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
          "Couldn't validate playlist encoding");
      return FALSE;
    }

    g_free (uri);
    if (download->redirect_permanent && download->redirect_uri) {
      uri = download->redirect_uri;
      base_uri = NULL;
    } else {
      uri = download->uri;
      base_uri = download->redirect_uri;
    }

    if (!gst_hls_demux_update_variant_playlist (demux, playlist, uri, base_uri)) {
      GST_WARNING_OBJECT (demux, "Failed to update the variant playlist");
      g_object_unref (download);
      g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
          "Couldn't update playlist");
      return FALSE;
    }

    g_object_unref (download);

    main_checked = TRUE;
    goto retry;
  }
  g_free (uri);

  m3u8 = demux->current_variant->m3u8;

  /* Set the base URI of the playlist to the redirect target if any */
  if (download->redirect_permanent && download->redirect_uri) {
    gst_m3u8_set_uri (m3u8, download->redirect_uri, NULL,
        demux->current_variant->name);
  } else {
    gst_m3u8_set_uri (m3u8, download->uri, download->redirect_uri,
        demux->current_variant->name);
  }

  buf = gst_fragment_get_buffer (download);
  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  gst_buffer_unref (buf);
  g_object_unref (download);

  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't validate playlist encoding");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
        "Couldn't validate playlist encoding");
    return FALSE;
  }

  if (!gst_m3u8_update (m3u8, playlist)) {
    if (gst_m3u8_is_live (m3u8)) {
      guint num_of_segments = g_list_length (m3u8->files);
      GST_WARNING_OBJECT (demux, "Couldn't update playlist");
      GST_INFO ("restart-live num-of-segments: %u", num_of_segments);
      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_element (GST_OBJECT_CAST (demux),
              gst_structure_new ("restart-live",
                  "num-of-segments", G_TYPE_UINT, num_of_segments, NULL)));
    }
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Couldn't update playlist");
    return FALSE;
  }

  if (gst_m3u8_is_live (m3u8) && main_checked && update) {
    GST_DEBUG_OBJECT (demux,
        "master playlist reloaded, try to update m3u8 in variant stream");
    if (!gst_hls_demux_stream_update_playlist (demux, m3u8))
      GST_WARNING_OBJECT (demux, "Couldn't find matching stream");
  }

  for (i = 0; i < GST_HLS_N_MEDIA_TYPES; ++i) {
    GList *mlist = demux->current_variant->media[i];

    while (mlist != NULL) {
      GstHLSMedia *media = mlist->data;

      if (media->uri == NULL) {
        /* No uri means this is a placeholder for a stream
         * contained in another mux */
        mlist = mlist->next;
        continue;
      }
      GST_LOG_OBJECT (demux,
          "Updating playlist for media of type %d - %s, uri: %s", i,
          media->name, media->uri);

      if (!gst_hls_demux_update_rendition_manifest (demux, media, err))
        return FALSE;

      if (update == FALSE && gst_m3u8_is_live (m3u8) &&
          (media->mtype == GST_HLS_MEDIA_TYPE_AUDIO ||
              media->mtype == GST_HLS_MEDIA_TYPE_VIDEO)) {
        GstM3U8 *media_playlist = media->playlist;

        if (!gst_hls_demux_align_live_rendition_streams (demux, media_playlist)) {
          g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
              "Couldn't align rendition streams");
          return FALSE;
        }
      }

      if (gst_m3u8_is_live (m3u8) && main_checked && update) {
        GST_DEBUG_OBJECT (demux,
            "master playlist reloaded, try to update m3u8 in rendition stream");
        if (!gst_hls_demux_stream_update_playlist (demux, media->playlist))
          GST_WARNING_OBJECT (demux, "Couldn't find matching stream");
      }

      mlist = mlist->next;
    }
  }

  /* If it's a live source, do not let the sequence number go beyond
   * three fragments before the end of the list */
  if (update == FALSE && gst_m3u8_is_live (m3u8)) {
    gint64 last_sequence, first_sequence;

    GST_M3U8_CLIENT_LOCK (demux->client);
    last_sequence =
        GST_M3U8_MEDIA_FILE (g_list_last (m3u8->files)->data)->sequence;
    first_sequence =
        GST_M3U8_MEDIA_FILE (g_list_first (m3u8->files)->data)->sequence;

    GST_DEBUG_OBJECT (demux,
        "sequence:%" G_GINT64_FORMAT " , first_sequence:%" G_GINT64_FORMAT
        " , last_sequence:%" G_GINT64_FORMAT, m3u8->sequence,
        first_sequence, last_sequence);
    if (m3u8->sequence > last_sequence - 3) {
      //demux->need_segment = TRUE;
      /* Make sure we never go below the minimum sequence number */
      m3u8->sequence = MAX (first_sequence, last_sequence - 3);
      GST_DEBUG_OBJECT (demux,
          "Sequence is beyond playlist. Moving back to %" G_GINT64_FORMAT,
          m3u8->sequence);
    }
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  } else if (!gst_m3u8_is_live (m3u8)) {
    GstClockTime current_pos, target_pos;
    guint sequence = 0;
    GList *walk;

    /* Sequence numbers are not guaranteed to be the same in different
     * playlists, so get the correct fragment here based on the current
     * position
     */
    GST_M3U8_CLIENT_LOCK (demux->client);

    /* Valid because hlsdemux only has a single output */
    if (GST_ADAPTIVE_DEMUX_CAST (demux)->streams) {
      GstAdaptiveDemuxStream *stream =
          GST_ADAPTIVE_DEMUX_CAST (demux)->streams->data;
      GstHLSDemuxStream *hls_stream = (GstHLSDemuxStream *) stream;
      target_pos = stream->segment.position - hls_stream->presentation_offset;
    } else {
      target_pos = 0;
    }
    if (GST_CLOCK_TIME_IS_VALID (m3u8->sequence_position)) {
      target_pos = MAX (target_pos, m3u8->sequence_position);
    }

    GST_LOG_OBJECT (demux, "Looking for sequence position %"
        GST_TIME_FORMAT " in updated playlist", GST_TIME_ARGS (target_pos));

    current_pos = 0;
    for (walk = m3u8->files; walk; walk = walk->next) {
      GstM3U8MediaFile *file = walk->data;

      sequence = file->sequence;
      if (current_pos <= target_pos
          && target_pos < current_pos + file->duration) {
        if (walk->next) {
          GstClockTime next_pos = current_pos + file->duration;
          if ((next_pos - target_pos) < target_pos - current_pos) {
            GST_WARNING_OBJECT (demux, "Possibly discontinuous PTS since"
                " target %" GST_TIME_FORMAT "is closer to next %"
                GST_TIME_FORMAT " than current %" GST_TIME_FORMAT,
                GST_TIME_ARGS (target_pos), GST_TIME_ARGS (next_pos),
                GST_TIME_ARGS (current_pos));

            sequence = GST_M3U8_MEDIA_FILE (walk->next->data)->sequence;
            current_pos += file->duration;
          }
        }
        break;
      }
      current_pos += file->duration;
    }
    /* End of playlist */
    if (!walk)
      sequence++;
    m3u8->sequence = sequence;
    m3u8->sequence_position = current_pos;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  }

  return TRUE;
}

static gboolean
gst_hls_demux_change_playlist (GstHLSDemux * demux, guint max_bitrate,
    gboolean * changed)
{
  GstHLSVariantStream *lowest_variant, *lowest_ivariant;
  GstHLSVariantStream *previous_variant, *new_variant;
  gint old_bandwidth, new_bandwidth;
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX_CAST (demux);
  GstAdaptiveDemuxStream *stream;

  g_return_val_if_fail (adaptive_demux->streams != NULL, FALSE);

  stream = adaptive_demux->streams->data;

  previous_variant = gst_hls_variant_stream_ref (demux->current_variant);
  new_variant =
      gst_hls_master_playlist_get_variant_for_bitrate (demux->master,
      demux->current_variant, max_bitrate);

  GST_M3U8_CLIENT_LOCK (demux->client);

retry_failover_protection:
  old_bandwidth = previous_variant->bandwidth;
  new_bandwidth = new_variant->bandwidth;

  /* Don't do anything else if the playlist is the same */
  if (new_bandwidth == old_bandwidth) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    gst_hls_variant_stream_unref (previous_variant);
    return TRUE;
  }

  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_hls_demux_set_current_variant (demux, new_variant);

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", old_bandwidth, max_bitrate, new_bandwidth);

  if (gst_hls_demux_update_playlist (demux, TRUE, NULL)) {
    const gchar *main_uri;
    gchar *uri;

    uri = gst_m3u8_get_uri (demux->current_variant->m3u8);
    main_uri = gst_adaptive_demux_get_manifest_ref_uri (adaptive_demux);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT_CAST (demux),
            gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                "manifest-uri", G_TYPE_STRING,
                main_uri, "uri", G_TYPE_STRING,
                uri, "bitrate", G_TYPE_INT, new_bandwidth, NULL)));
    g_free (uri);
    if (changed)
      *changed = TRUE;
    stream->discont = TRUE;
  } else if (gst_adaptive_demux_is_running (GST_ADAPTIVE_DEMUX_CAST (demux))) {
    GstHLSVariantStream *failover_variant = NULL;
    GList *failover;

    GST_INFO_OBJECT (demux, "Unable to update playlist. Switching back");
    GST_M3U8_CLIENT_LOCK (demux->client);

    /* we find variants by bitrate by going from highest to lowest, so it's
     * possible that there's another variant with the same bitrate before the
     * one selected which we can use as failover */
    failover = g_list_find (demux->master->variants, new_variant);
    if (failover != NULL)
      failover = failover->prev;
    if (failover != NULL)
      failover_variant = failover->data;
    if (failover_variant && new_bandwidth == failover_variant->bandwidth) {
      new_variant = failover_variant;
      goto retry_failover_protection;
    }

    GST_M3U8_CLIENT_UNLOCK (demux->client);
    gst_hls_demux_set_current_variant (demux, previous_variant);
    /*  Try a lower bitrate (or stop if we just tried the lowest) */
    if (previous_variant->iframe) {
      lowest_ivariant = demux->master->iframe_variants->data;
      if (new_bandwidth == lowest_ivariant->bandwidth) {
        gst_hls_variant_stream_unref (previous_variant);
        return FALSE;
      }
    } else {
      lowest_variant = demux->master->variants->data;
      if (new_bandwidth == lowest_variant->bandwidth) {
        gst_hls_variant_stream_unref (previous_variant);
        return FALSE;
      }
    }
    gst_hls_variant_stream_unref (previous_variant);
    return gst_hls_demux_change_playlist (demux, new_bandwidth - 1, changed);
  }
  gst_hls_variant_stream_unref (previous_variant);
  return TRUE;
}

#if defined(HAVE_OPENSSL)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  EVP_CIPHER_CTX *ctx;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  EVP_CIPHER_CTX_init (&stream->aes_ctx);
  ctx = &stream->aes_ctx;
#else
  stream->aes_ctx = EVP_CIPHER_CTX_new ();
  ctx = stream->aes_ctx;
#endif
  if (!EVP_DecryptInit_ex (ctx, EVP_aes_128_cbc (), NULL, key_data, iv_data))
    return FALSE;
  EVP_CIPHER_CTX_set_padding (ctx, 0);
  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  int len, flen = 0;
  EVP_CIPHER_CTX *ctx;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  ctx = &stream->aes_ctx;
#else
  ctx = stream->aes_ctx;
#endif

  if (G_UNLIKELY (length > G_MAXINT || length % 16 != 0))
    return FALSE;

  len = (int) length;
  if (!EVP_DecryptUpdate (ctx, decrypted_data, &len, encrypted_data, len))
    return FALSE;
  EVP_DecryptFinal_ex (ctx, decrypted_data + len, &flen);
  g_return_val_if_fail (len + flen == length, FALSE);
  return TRUE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  EVP_CIPHER_CTX_cleanup (&stream->aes_ctx);
#else
  EVP_CIPHER_CTX_free (stream->aes_ctx);
  stream->aes_ctx = NULL;
#endif
}

#elif defined(HAVE_NETTLE)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  aes_set_decrypt_key (&stream->aes_ctx.ctx, 16, key_data);
  CBC_SET_IV (&stream->aes_ctx, iv_data);

  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  if (length % 16 != 0)
    return FALSE;

  CBC_DECRYPT (&stream->aes_ctx, aes_decrypt, length, decrypted_data,
      encrypted_data);

  return TRUE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  /* NOP */
}

#else
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  gcry_error_t err = 0;
  gboolean ret = FALSE;

  err =
      gcry_cipher_open (&stream->aes_ctx, GCRY_CIPHER_AES128,
      GCRY_CIPHER_MODE_CBC, 0);
  if (err)
    goto out;
  err = gcry_cipher_setkey (stream->aes_ctx, key_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_setiv (stream->aes_ctx, iv_data, 16);
  if (!err)
    ret = TRUE;

out:
  if (!ret)
    if (stream->aes_ctx)
      gcry_cipher_close (stream->aes_ctx);

  return ret;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  gcry_error_t err = 0;

  err = gcry_cipher_decrypt (stream->aes_ctx, decrypted_data, length,
      encrypted_data, length);

  return err == 0;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  if (stream->aes_ctx) {
    gcry_cipher_close (stream->aes_ctx);
    stream->aes_ctx = NULL;
  }
}
#endif

static GstBuffer *
gst_hls_demux_decrypt_fragment (GstHLSDemux * demux, GstHLSDemuxStream * stream,
    GstBuffer * encrypted_buffer, GError ** err)
{
  GstBuffer *decrypted_buffer = NULL;
  GstMapInfo encrypted_info, decrypted_info;

  decrypted_buffer =
      gst_buffer_new_allocate (NULL, gst_buffer_get_size (encrypted_buffer),
      NULL);

  gst_buffer_map (encrypted_buffer, &encrypted_info, GST_MAP_READ);
  gst_buffer_map (decrypted_buffer, &decrypted_info, GST_MAP_WRITE);

  if (!decrypt_fragment (stream, encrypted_info.size,
          encrypted_info.data, decrypted_info.data))
    goto decrypt_error;


  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);

  return decrypted_buffer;

decrypt_error:
  GST_ERROR_OBJECT (demux, "Failed to decrypt fragment");
  g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_DECRYPT,
      "Failed to decrypt fragment");

  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);
  gst_buffer_unref (decrypted_buffer);

  return NULL;
}

static gint64
gst_hls_demux_get_manifest_update_interval (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime reload_interval;

  if (hlsdemux->current_variant) {
    reload_interval =
        gst_m3u8_get_reload_interval (hlsdemux->current_variant->m3u8);
  } else {
    reload_interval = 5 * GST_SECOND;
  }

  GST_INFO_OBJECT (demux, "reload interval is %" G_GUINT64_FORMAT,
      GST_TIME_AS_MSECONDS (reload_interval));

  return gst_util_uint64_scale (reload_interval, G_USEC_PER_SEC, GST_SECOND);
}

static gboolean
gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean ret = FALSE;

  if (hlsdemux->current_variant) {
    ret =
        gst_m3u8_get_seek_range (hlsdemux->current_variant->m3u8, start, stop);
  }

  return ret;
}

static void
gst_hls_demux_notify_adaptive_streaming_resource (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GList *iter;
  GstStructure *structure;
  guint i;

  if (hlsdemux->master == NULL || hlsdemux->master->variants == NULL) {
    GST_WARNING_OBJECT (demux, "No available playlist");
    return;
  }

  structure =
      gst_structure_new_empty (GST_ADAPTIVE_DEMUX_RESOURCE_MESSAGE_NAME);

  for (iter = hlsdemux->master->variants, i = 0; iter;
      iter = g_list_next (iter), i++) {
    GstHLSVariantStream *stream = iter->data;
    gchar *string;
    gchar stream_name[128];
    GstStructure *stream_str;

    g_sprintf (stream_name, "variant-%u", i);

    /* TODO: we might extract framerate using FRAME-RATE tag */
    stream_str =
        gst_structure_new (stream_name, "uri", G_TYPE_STRING, stream->uri,
        "codecs", G_TYPE_STRING, GST_STR_NULL (stream->codecs), "bitrate",
        G_TYPE_INT, stream->bandwidth, "width", G_TYPE_INT, stream->width,
        "height", G_TYPE_INT, stream->height, "iframe", G_TYPE_BOOLEAN,
        stream->iframe, "is-simple", G_TYPE_BOOLEAN,
        hlsdemux->master->is_simple, NULL);

    string = gst_structure_to_string (stream_str);

    GST_LOG_OBJECT (demux, "Add %s field in %s, %s", stream_name,
        GST_ADAPTIVE_DEMUX_RESOURCE_MESSAGE_NAME, string);
    g_free (string);

    gst_structure_set (structure, stream_name, GST_TYPE_STRUCTURE, stream_str,
        NULL);

    gst_structure_free (stream_str);
  }

  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux), structure));
}
