/* GStreamer unit test for Clearkey DRM
 *
 * Copyright (c) <2016> LG Electronics Inc.
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

#include <gst/check/gstcheck.h>

#define TOTAL_SIZE 1403415

static GstBuffer *buf;
static guint offset;

static void
cb_new_pad1 (GstElement * element, GstPad * pad, gpointer data)
{
  gchar *name;
  GstElement *qtdemux = data;

  name = gst_pad_get_name (pad);
  if (strcmp (name, "video_00") == 0
      && !gst_element_link_pads (element, name, qtdemux, "sink")) {
    g_printerr ("link dashdemux-qtdemux fail\n");
  }
  g_free (name);
}

static void
cb_new_pad2 (GstElement * element, GstPad * pad, gpointer data)
{
  gchar *name;
  GstElement *ckdrm = data;

  name = gst_pad_get_name (pad);
  if (strcmp (name, "video_0") == 0
      && !gst_element_link_pads (element, name, ckdrm, "sink")) {
    g_printerr ("link qtdemux-ckdrm fail\n");
  }
  g_free (name);
}

static gboolean
on_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_assert_not_reached ();
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static GstPadProbeReturn
cb_dump_data (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  // dump decrypted stream
  GstMapInfo map;
  GstBuffer *buffer;
  GstBuffer *buf = user_data;
  buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  if (buffer == NULL)
    return GST_PAD_PROBE_OK;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  gst_buffer_fill (buf, offset, map.data, map.size);
  gst_buffer_unmap (buffer, &map);
  offset += map.size;

  return GST_PAD_PROBE_OK;
}

/*
 * Test to decrypt AES-CTR mode.
 *
 */
GST_START_TEST (ckdrm_decryption)
{
  GstElement *pipeline, *filesrc, *dashdemux, *qtdemux, *ckdrm, *sink;
  GMainLoop *loop;
  GstBus *bus;
  GstPad *pad;
  gboolean ret = TRUE;
  gchar *path;

  guint8 decrypted_sample[] = {
    0x00, 0x00, 0x02, 0xB3, 0x06, 0x05, 0xFF, 0xFF,
    0xAF, 0xDC, 0x45, 0xE9, 0xBD, 0xE6, 0xD9, 0x48,
    0xB7, 0x96, 0x2C, 0xD8, 0x20, 0xD9, 0x23, 0xEE,
    0xEF, 0x78, 0x32, 0x36, 0x34, 0x20, 0x2D, 0x20,
    0x63, 0x6F, 0x72, 0x65, 0x20, 0x31, 0x34, 0x32,
    0x20, 0x72, 0x32, 0x33, 0x38, 0x39, 0x20, 0x39,
    0x35, 0x36, 0x63, 0x38, 0x64, 0x38, 0x20, 0x2D,
    0x20, 0x48, 0x2E, 0x32, 0x36, 0x34, 0x2F, 0x4D,
    0x50, 0x45, 0x47, 0x2D, 0x34, 0x20, 0x41, 0x56,
    0x43, 0x20, 0x63, 0x6F, 0x64, 0x65, 0x63, 0x20,
    0x2D, 0x20, 0x43, 0x6F, 0x70, 0x79, 0x6C, 0x65,
    0x66, 0x74, 0x20, 0x32, 0x30, 0x30, 0x33, 0x2D,
    0x32, 0x30, 0x31, 0x34, 0x20, 0x2D, 0x20, 0x68
  };

  offset = 0;
  buf = gst_buffer_new_and_alloc (TOTAL_SIZE);

  loop = g_main_loop_new (NULL, FALSE);

  /* build pipeline */
  pipeline = gst_pipeline_new ("pipeline");
  filesrc = gst_element_factory_make ("filesrc", NULL);
  dashdemux = gst_element_factory_make ("dashdemux", NULL);
  qtdemux = gst_element_factory_make ("qtdemux", NULL);
  ckdrm = gst_element_factory_make ("ckdrm", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add_many (GST_BIN (pipeline), filesrc, dashdemux, qtdemux, ckdrm,
      sink, NULL);

  fail_unless (gst_element_link (filesrc, dashdemux));

  path = g_build_filename (GST_TEST_FILES_PATH, "cablelabs_clearkey.mpd", NULL);
  g_print ("reading file '%s'", path);
  g_object_set (G_OBJECT (filesrc), "location", path, NULL);

  g_signal_connect (dashdemux, "pad-added", G_CALLBACK (cb_new_pad1), qtdemux);
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (cb_new_pad2), ckdrm);

  fail_unless (gst_element_link (ckdrm, sink));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (on_message), loop);
  gst_object_unref (GST_OBJECT (bus));

  pad = gst_element_get_static_pad (ckdrm, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) cb_dump_data, buf, NULL);
  gst_object_unref (pad);

  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    assert_equals_int (FALSE, TRUE);
  }

  g_print ("Running\n");
  g_main_loop_run (loop);

  g_print ("Stopping Playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Quitting\n");
  g_object_unref (G_OBJECT (pipeline));

  fail_if (gst_buffer_memcmp (buf, 0, decrypted_sample,
          sizeof (decrypted_sample)));

  g_main_loop_unref (loop);
  gst_buffer_unref (buf);

  assert_equals_int (ret, TRUE);
}

GST_END_TEST;

/*
 * create a test suite containing all ckdrm testcases
 */
static Suite *
ckdrm_suite (void)
{
  Suite *s = suite_create ("ckdrm");
  TCase *tc_decryption = tcase_create ("decryption");
  suite_add_tcase (s, tc_decryption);
  tcase_add_loop_test (tc_decryption, ckdrm_decryption, 1, 2);

  return s;
}

GST_CHECK_MAIN (ckdrm);
