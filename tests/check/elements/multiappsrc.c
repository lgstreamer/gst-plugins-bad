/* GStreamer
 *
 * unit test for multiappsrc
 *
 * Copyright (C) 2014-2015 LG Electronics, Inc.
 *           Myoungsun Lee <mysunny.lee@lge.com>
 *
 * Copyright (C) 2015 Collabora Ltd.
 *           Wonchul Lee <wonchul.lee@collabora.com>
 *           Justin Kim <justin.kim@collabora.com>
 *
 * Copyright (C) 2016 LG Electronics, Inc.
 *           Seungha Yang <sh.yang@lge.com>
 *           Changbok Chea <changbok.chea@lge.com>
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/multiapp/gstmultiappsrc.h>

GST_START_TEST (test_uri_interface)
{
  GstElement *multiappsrc;
  const gchar *const *uri_protocols;

  multiappsrc = gst_element_factory_make ("multiappsrc", "multiappsrc");
  fail_unless (multiappsrc != NULL, "Failed to create multiappsrc element");
  fail_unless (GST_IS_URI_HANDLER (multiappsrc),
      "Not implemented as URI handler");

  uri_protocols = gst_uri_handler_get_protocols (GST_URI_HANDLER (multiappsrc));

  fail_unless (uri_protocols && *uri_protocols,
      "Cannot get supported protocol information");

  fail_unless (!g_strcmp0 (*uri_protocols, "multiappsrc"),
      "Not supported 'multiappsrc' protocol");

  gst_object_unref (multiappsrc);
}

GST_END_TEST;

GMutex global_lock;

static void
pad_added_cb (GstElement * element, GstPad * pad, GstElement * pipe)
{
  GstElement *sink = NULL;
  GstPad *sinkpad = NULL;
  GST_DEBUG_OBJECT (pad, "pad-added");

  g_mutex_lock (&global_lock);
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink, "cannot create fakesink");
  g_object_set (GST_OBJECT (sink), "async", FALSE, NULL);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink),
      "fail to add fakesink to bin");
  fail_unless (gst_element_sync_state_with_parent (sink),
      "fail to sync state of fakesink");

  fail_unless (sinkpad = gst_element_get_static_pad (sink, "sink"),
      "fail to get sinkpad of fakesink");

  fail_unless (gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK, "fail to link");

  gst_object_unref (sinkpad);

  g_mutex_unlock (&global_lock);
  GST_DEBUG_OBJECT (pad, "return pad-added");
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GstElement * pipe)
{
  GstElement *sinkelement = NULL;
  GstIterator *it;
  gboolean done = FALSE;
  GValue data = G_VALUE_INIT;
  GST_DEBUG_OBJECT (pad, "pad-removed");

  g_mutex_lock (&global_lock);
  it = gst_bin_iterate_sinks (GST_BIN (pipe));
  while (!done) {
    switch (gst_iterator_next (it, &data)) {
      case GST_ITERATOR_OK:
      {
        GstPad *sinkpad = NULL;
        GstPad *peerpad = NULL;
        sinkelement = (GstElement *) g_value_get_object (&data);
        sinkpad = gst_element_get_static_pad (sinkelement, "sink");

        fail_unless (sinkpad != NULL, "fail to get sink pad from sinkelement");
        peerpad = gst_pad_get_peer (sinkpad);
        if (peerpad == NULL) {
          GST_DEBUG_OBJECT (sinkelement, "remove unliked element");
          gst_element_set_state (sinkelement, GST_STATE_NULL);
          fail_unless (gst_bin_remove (GST_BIN (pipe), sinkelement),
              "fail to remove unlinked sink element from pipeline");
        } else {
          gst_object_unref (peerpad);
        }

        sinkelement = NULL;
        gst_object_unref (sinkpad);
        g_value_reset (&data);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&data);
  gst_iterator_free (it);
  g_mutex_unlock (&global_lock);

  GST_DEBUG_OBJECT (pad, "return pad-removed");
}

static void
no_more_pads_cb (GstElement * element, gboolean * finished)
{
  *finished = TRUE;
}

GST_START_TEST (test_multiappsrc_add_remove)
{
  GstElement *multiappsrc, *pipe;
  gchar *source_id1 = NULL, *source_id2 = NULL, *source_id3 = NULL;
  gint n_source = 0;
  gboolean finished = FALSE;
  GstMessage *msg;
  gboolean ret;
  GstBuffer *buffer;
  GstFlowReturn flow_ret;

  /* In this test, add-source-id and remove-source-id methods
   * will be tested.
   * For testing the add-source-id method, two appsrc's will be created before PAUSED state.
   * After then, one more appsrc will be added in PLAYING state
   * For testing the remove-source-id, one appsrc will be removed in PLAYING state. */

  g_mutex_init (&global_lock);

  /* Setup pipeline with multiappsrc */
  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  pipe = gst_pipeline_new ("pipeline");
  fail_unless (multiappsrc != NULL,
      "fail to create multiappsrc element by uri");
  fail_unless (pipe != NULL, "fail to create pipeline");
  gst_bin_add (GST_BIN (pipe), multiappsrc);

  /* sinkpad will be made by pad-added callback */
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);
  g_signal_connect (multiappsrc, "no-more-pads",
      G_CALLBACK (no_more_pads_cb), &finished);

  /*** [TEST] add-source-id **************************************************/

  /* STEP1: create two appsrc's before PAUSED state */
  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id1);
  fail_unless (source_id1, "failed to create 1st source");

  g_signal_emit_by_name (multiappsrc, "add-source-id", "appsrc1", &source_id2);
  fail_unless (source_id2, "failed to create 2nd source");

  /* cannot request add-source-id with duplicated name (appsrc1) */
  g_signal_emit_by_name (multiappsrc, "add-source-id", "appsrc1", &source_id3);
  fail_unless (!source_id3, "new appsrc was generated with duplicated name");

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 2, "the number of appsrce elements is not matched.");

  fail_unless (!finished, "no-more-pads should not be present here.");

  ASSERT_SET_STATE (pipe, GST_STATE_PAUSED, GST_STATE_CHANGE_SUCCESS);

  /* push buffer using signal-action to each appsrc.
   * Note that a buffer which pushed by using "push-buffer" signal-action should
   * be unref at caller side, because multiappsrc does not take ownership in this case */
  buffer = gst_buffer_new_and_alloc (4);
  g_signal_emit_by_name (multiappsrc, "push-buffer", source_id1, buffer,
      &flow_ret);
  fail_unless (flow_ret == GST_FLOW_OK,
      "fail to push buffer using signal-action");
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (4);
  g_signal_emit_by_name (multiappsrc, "push-buffer", source_id2, buffer,
      &flow_ret);
  fail_unless (flow_ret == GST_FLOW_OK,
      "fail to push buffer using signal-action");
  gst_buffer_unref (buffer);

  ASSERT_SET_STATE (pipe, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  /* STEP2: create one more appsrc in PLAYING STATE */
  g_signal_emit_by_name (multiappsrc, "add-source-id", "appsrc2", &source_id3);
  fail_unless (source_id3, "failed to create 3rd source");

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 3, "newly added appsrc was not considered %d",
      n_source);

  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id3, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");

  /*** [TEST] remove-source-id ***********************************************/
  /* Step1: remove the first appsrc */
  g_signal_emit_by_name (multiappsrc, "remove-source-id", source_id1, &ret);
  fail_unless (ret, "cannot remove source_id (%s)", source_id1);

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 2, "removed appsrc was not considered, %d",
      n_source);

  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, gst_buffer_new_and_alloc (4)) == GST_FLOW_ERROR,
      "unexpected flow return");
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id3, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");

  /* Step2: remove one more appsrc */
  g_signal_emit_by_name (multiappsrc, "remove-source-id", source_id2, &ret);
  fail_unless (ret, "cannot remove source_id (%s)", source_id2);

  /* Step3: remove the last appsrc. during playing state, this is prohibited */
  g_signal_emit_by_name (multiappsrc, "remove-source-id", source_id3, &ret);
  fail_unless (!ret, "the last appsrc was removed, source_id (%s)", source_id3);

  /* send end-of-stream */
  fail_unless (gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc),
          NULL) == GST_FLOW_OK, "fail to send eos");

  msg =
      gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (msg);

  ASSERT_SET_STATE (pipe, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS);

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 0,
      "all of internal appsrce element should be removed.");

  ASSERT_SET_STATE (pipe, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);

  g_mutex_clear (&global_lock);
  gst_object_unref (pipe);
  g_free (source_id1);
  g_free (source_id2);
  g_free (source_id3);
}

GST_END_TEST;

typedef struct _SeekDataTestVector
{
  guint cnt;
  GMutex lock;
} SeekDataTestVector;

static gboolean
seek_data_cb (GstElement * multiappsrc, gchar * source_id, guint64 position,
    SeekDataTestVector * vector)
{
  GST_DEBUG ("seek-data from source id (%s), %" G_GUINT64_FORMAT,
      source_id, position);
  g_mutex_lock (&vector->lock);
  vector->cnt++;
  g_mutex_unlock (&vector->lock);

  return TRUE;
}


GST_START_TEST (test_multiappsrc_send_seek)
{
  GstElement *multiappsrc, *pipe;
  gchar *source_id1 = NULL, *source_id2 = NULL;
  GstMessage *msg;
  SeekDataTestVector *vector;

  vector = g_malloc0 (sizeof (SeekDataTestVector));
  vector->cnt = 0;
  g_mutex_init (&vector->lock);
  g_mutex_init (&global_lock);
  /* Setup pipeline with multiappsrc */
  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  pipe = gst_pipeline_new ("pipeline");
  fail_unless (multiappsrc != NULL,
      "fail to create multiappsrc element by uri");
  fail_unless (pipe != NULL, "fail to create pipeline");
  gst_bin_add (GST_BIN (pipe), multiappsrc);

  /* sinkpad will be made by pad-added callback */
  g_object_set (multiappsrc, "stream-type", GST_APP_STREAM_TYPE_SEEKABLE,
      "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);
  g_signal_connect (multiappsrc, "seek-data", G_CALLBACK (seek_data_cb),
      vector);

  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id1);
  fail_unless (source_id1, "failed to create 1st source");

  g_signal_emit_by_name (multiappsrc, "add-source-id", "appsrc1", &source_id2);
  fail_unless (source_id2, "failed to create 2nd source");

  ASSERT_SET_STATE (pipe, GST_STATE_PAUSED, GST_STATE_CHANGE_SUCCESS);
  fail_unless (vector->cnt == 2,
      "initial seek-data callback does not detected");

  /* push buffer to each appsrc */
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");

  ASSERT_SET_STATE (pipe, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);


  GST_DEBUG ("Send seek to 10 sec");
  gst_element_seek (multiappsrc, 1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 10 * GST_SECOND,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");

  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer");

  fail_unless (vector->cnt == 4, "send seek event does not work");
  /* send end-of-stream */
  fail_unless (gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc),
          NULL) == GST_FLOW_OK, "fail to send eos");

  msg =
      gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (msg);

  ASSERT_SET_STATE (pipe, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (pipe);
  g_mutex_clear (&vector->lock);
  g_mutex_clear (&global_lock);
  g_free (vector);
  g_free (source_id1);
  g_free (source_id2);
}

GST_END_TEST;

GST_START_TEST (test_multiappsrc_push_discont)
{
  GstElement *multiappsrc, *pipe;
  gchar *source_id1 = NULL, *source_id2 = NULL;
  gint n_source = 0;
  gboolean finished = FALSE;
  GstMessage *msg;
  GstBuffer *buffer;
  guint i;
  SeekDataTestVector *vector;

  vector = g_malloc0 (sizeof (SeekDataTestVector));
  vector->cnt = 0;
  g_mutex_init (&vector->lock);
  g_mutex_init (&global_lock);

  /* Setup pipeline with multiappsrc */
  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  pipe = gst_pipeline_new ("pipeline");
  fail_unless (multiappsrc != NULL,
      "fail to create multiappsrc element by uri");
  fail_unless (pipe != NULL, "fail to create pipeline");
  gst_bin_add (GST_BIN (pipe), multiappsrc);

  /* sinkpad will be made by pad-added callback */
  g_object_set (multiappsrc, "stream-type", GST_APP_STREAM_TYPE_SEEKABLE,
      "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);
  g_signal_connect (multiappsrc, "no-more-pads",
      G_CALLBACK (no_more_pads_cb), &finished);
  g_signal_connect (multiappsrc, "seek-data", G_CALLBACK (seek_data_cb),
      vector);


  /* STEP1: create two appsrc's before PAUSED state */
  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id1);
  fail_unless (source_id1, "failed to create 1st source");

  g_signal_emit_by_name (multiappsrc, "add-source-id", "appsrc1", &source_id2);
  fail_unless (source_id2, "failed to create 2nd source");

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 2, "the number of appsrce elements is not matched.");

  fail_unless (!finished, "no-more-pads should not be present here.");

  ASSERT_SET_STATE (pipe, GST_STATE_PAUSED, GST_STATE_CHANGE_SUCCESS);
  fail_unless (vector->cnt == 2,
      "initial seek-data callback does not detected");

  /* push buffer to each appsrc */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = 1 * GST_SECOND;
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, buffer) == GST_FLOW_OK, "fail to push buffer");
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = 1 * GST_SECOND;
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, buffer) == GST_FLOW_OK, "fail to push buffer");

  ASSERT_SET_STATE (pipe, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  for (i = 2; i < 10; i++) {
    /* Push repeated discont buffer */
    buffer = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = i * GST_SECOND;
    fail_unless (gst_multi_appsrc_push_discont_buffer (GST_MULTI_APPSRC
            (multiappsrc), source_id1, buffer)
        == GST_FLOW_OK, "fail to push buffer");

    buffer = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = i * GST_SECOND;
    fail_unless (gst_multi_appsrc_push_discont_buffer (GST_MULTI_APPSRC
            (multiappsrc), source_id2, buffer)
        == GST_FLOW_OK, "fail to push buffer");
  }

  fail_unless (vector->cnt == 2,
      "push_discont_buffer () should not post seek_data_cb to application");

  /* send end-of-stream */
  fail_unless (gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc),
          NULL) == GST_FLOW_OK, "fail to send eos");

  msg =
      gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (msg);

  ASSERT_SET_STATE (pipe, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS);

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 0,
      "all of internal appsrce element should be removed.");

  ASSERT_SET_STATE (pipe, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);

  g_mutex_clear (&vector->lock);
  g_mutex_clear (&global_lock);
  g_free (vector);
  gst_object_unref (pipe);
  g_free (source_id1);
  g_free (source_id2);
}

GST_END_TEST;

GST_START_TEST (test_multiappsrc_push_discont_sample)
{
  GstElement *multiappsrc, *pipe;
  gchar *source_id1 = NULL, *source_id2 = NULL;
  gint n_source = 0;
  gboolean finished = FALSE;
  GstMessage *msg;
  GstBuffer *buffer;
  guint i;
  SeekDataTestVector *vector;

  vector = g_malloc0 (sizeof (SeekDataTestVector));
  vector->cnt = 0;
  g_mutex_init (&vector->lock);
  g_mutex_init (&global_lock);

  /* Setup pipeline with multiappsrc */
  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  pipe = gst_pipeline_new ("pipeline");
  fail_unless (multiappsrc != NULL,
      "fail to create multiappsrc element by uri");
  fail_unless (pipe != NULL, "fail to create pipeline");
  gst_bin_add (GST_BIN (pipe), multiappsrc);

  /* sinkpad will be made by pad-added callback */
  g_object_set (multiappsrc, "stream-type", GST_APP_STREAM_TYPE_SEEKABLE,
      "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);
  g_signal_connect (multiappsrc, "no-more-pads",
      G_CALLBACK (no_more_pads_cb), &finished);
  g_signal_connect (multiappsrc, "seek-data", G_CALLBACK (seek_data_cb),
      vector);


  /* STEP1: create two appsrc's before PAUSED state */
  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id1);
  fail_unless (source_id1, "failed to create 1st source");

  g_signal_emit_by_name (multiappsrc, "add-source-id", "appsrc1", &source_id2);
  fail_unless (source_id2, "failed to create 2nd source");

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 2, "the number of appsrce elements is not matched.");

  fail_unless (!finished, "no-more-pads should not be present here.");

  ASSERT_SET_STATE (pipe, GST_STATE_PAUSED, GST_STATE_CHANGE_SUCCESS);
  fail_unless (vector->cnt == 2,
      "initial seek-data callback does not detected");

  /* push buffer to each appsrc */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = 1 * GST_SECOND;
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, buffer) == GST_FLOW_OK, "fail to push buffer");
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = 1 * GST_SECOND;
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, buffer) == GST_FLOW_OK, "fail to push buffer");

  ASSERT_SET_STATE (pipe, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  for (i = 2; i < 10; i++) {
    GstSegment *segment;
    GstSample *sample;
    GstCaps *caps;
    /* Push repeated discont buffer */
    segment = gst_segment_new ();
    gst_segment_init (segment, GST_FORMAT_TIME);
    segment->start = i * GST_SECOND;

    buffer = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = i * GST_SECOND;

    caps = gst_caps_from_string ("foo/bar");

    sample = gst_sample_new (buffer, caps, segment, NULL);
    fail_unless (gst_multi_appsrc_push_discont_sample (GST_MULTI_APPSRC
            (multiappsrc), source_id1, sample)
        == GST_FLOW_OK, "fail to push sample");

    gst_buffer_unref (buffer);
    gst_caps_unref (caps);
    gst_segment_free (segment);
    gst_sample_unref (sample);

    segment = gst_segment_new ();
    gst_segment_init (segment, GST_FORMAT_TIME);
    segment->start = i * GST_SECOND;

    buffer = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = i * GST_SECOND;

    caps = gst_caps_from_string ("foo/bar");

    sample = gst_sample_new (buffer, NULL, segment, NULL);
    fail_unless (gst_multi_appsrc_push_discont_sample (GST_MULTI_APPSRC
            (multiappsrc), source_id2, sample)
        == GST_FLOW_OK, "fail to push sample");

    gst_buffer_unref (buffer);
    gst_caps_unref (caps);
    gst_segment_free (segment);
    gst_sample_unref (sample);
  }

  fail_unless (vector->cnt == 2,
      "push_discont_buffer () should not post seek_data_cb to application");

  /* send end-of-stream */
  fail_unless (gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc),
          NULL) == GST_FLOW_OK, "fail to send eos");

  msg =
      gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (msg);

  ASSERT_SET_STATE (pipe, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS);

  g_object_get (multiappsrc, "n-source", &n_source, NULL);
  fail_unless (n_source == 0,
      "all of internal appsrce element should be removed.");

  ASSERT_SET_STATE (pipe, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);

  g_mutex_clear (&vector->lock);
  g_mutex_clear (&global_lock);
  g_free (vector);
  gst_object_unref (pipe);
  g_free (source_id1);
  g_free (source_id2);
}

GST_END_TEST;

static void
start_feed (GstMultiAppSrc * multiappsrc, gchar * source_id,
    guint length, gpointer data)
{
  guint *pushCount = (guint *) data;
  GstBuffer *buffer;

  if (*pushCount == 100) {
    gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc), NULL);
    return;
  }

  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer) = (*pushCount) * GST_SECOND;
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id, buffer) == GST_FLOW_OK,
      "fail to push buffer to source id %s", source_id);

  (*pushCount)++;
}


GST_START_TEST (test_multiappsrc_pull_model_streaming)
{
  GstElement *multiappsrc, *pipe;
  gchar *source_id = NULL;
  GstMessage *msg;
  guint pushCount = 0;

  g_mutex_init (&global_lock);

  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  pipe = gst_pipeline_new ("pipeline");
  fail_unless (multiappsrc != NULL,
      "fail to create multiappsrc element by uri");
  fail_unless (pipe != NULL, "fail to create pipeline");
  gst_bin_add (GST_BIN (pipe), multiappsrc);

  /* sinkpad will be made by pad-added callback */
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);
  g_signal_connect (multiappsrc, "need-data", G_CALLBACK (start_feed),
      &pushCount);

  source_id =
      gst_multi_appsrc_add_source_id (GST_MULTI_APPSRC (multiappsrc), NULL);
  fail_unless (source_id);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg =
      gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipe, GST_STATE_NULL);

  g_mutex_clear (&global_lock);
  gst_object_unref (pipe);
  g_free (source_id);
}

GST_END_TEST;

static void
start_discont_feed (GstMultiAppSrc * multiappsrc, gchar * source_id,
    guint length, gpointer data)
{
  guint *pushCount = (guint *) data;
  GstBuffer *buffer;

  if (*pushCount == 50) {
    buffer = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer)
        = (*pushCount) * GST_SECOND;
    fail_unless (gst_multi_appsrc_push_discont_buffer (GST_MULTI_APPSRC
            (multiappsrc), source_id, buffer) == GST_FLOW_OK,
        "fail to push discont buffer to source id %s", source_id);

    (*pushCount)++;
    return;
  }

  if (*pushCount == 100) {
    gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc), NULL);
    return;
  }

  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buffer) = GST_BUFFER_DTS (buffer)
      = (*pushCount) * GST_SECOND;
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id, buffer) == GST_FLOW_OK,
      "fail to push buffer to source id %s", source_id);

  (*pushCount)++;
}

GST_START_TEST (test_multiappsrc_pull_model_discont_streaming)
{
  GstElement *multiappsrc, *pipe;
  gchar *source_id = NULL;
  GstMessage *msg;
  guint pushCount = 0;

  g_mutex_init (&global_lock);
  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  pipe = gst_pipeline_new ("pipeline");
  fail_unless (multiappsrc != NULL,
      "fail to create multiappsrc element by uri");
  fail_unless (pipe != NULL, "fail to create pipeline");
  gst_bin_add (GST_BIN (pipe), multiappsrc);

  /* sinkpad will be made by pad-added callback */
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);
  g_signal_connect (multiappsrc, "need-data", G_CALLBACK (start_discont_feed),
      &pushCount);

  source_id =
      gst_multi_appsrc_add_source_id (GST_MULTI_APPSRC (multiappsrc), NULL);
  fail_unless (source_id);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg =
      gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipe, GST_STATE_NULL);

  g_mutex_clear (&global_lock);
  gst_object_unref (pipe);
  g_free (source_id);
}

GST_END_TEST;

GST_START_TEST (test_smart_properties_after_create)
{
  GstElement *multiappsrc;
  GstElement *appsrc1, *appsrc2;
  GstStructure *smart_prop;
  GstStructure *result1 = NULL, *result2 = NULL;
  gchar *source_id1 = NULL, *source_id2 = NULL;

  smart_prop = gst_structure_new ("smart-properties",
      "test", G_TYPE_INT, 10, NULL);

  /* step 1. set smart-properties before creating appsrc element */
  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);
  g_object_set (multiappsrc, "smart-properties", smart_prop, NULL);

  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id1);
  fail_unless (source_id1, "failed to create 1st source");

  g_signal_emit_by_name (multiappsrc, "get-appsrc", source_id1, &appsrc1);
  fail_unless (appsrc1, "failed to get appsrc1");

  g_object_get (appsrc1, "smart-properties", &result1, NULL);

  fail_unless (gst_structure_is_equal (smart_prop, result1),
      "step1:smart-properties does not propagate to appsrc.");
  gst_structure_free (result1);

  /* step 2. add more appsrc element */
  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id2);
  fail_unless (source_id2, "failed to create 2st source");

  g_signal_emit_by_name (multiappsrc, "get-appsrc", source_id2, &appsrc2);
  fail_unless (appsrc2, "failed to get appsrc2");

  gst_structure_set (smart_prop, "test2", G_TYPE_BOOLEAN, TRUE, NULL);

  g_object_set (multiappsrc, "smart-properties", smart_prop, NULL);

  g_object_get (appsrc1, "smart-properties", &result1, NULL);
  g_object_get (appsrc2, "smart-properties", &result2, NULL);

  fail_unless (gst_structure_is_equal (smart_prop, result1),
      "step2:smart-properties does not propagate to appsrc.");
  fail_unless (gst_structure_is_equal (smart_prop, result2),
      "step2:smart-properties does not propagate to appsrc.");

  /* free appsrc explicitly if "get-appsrc" was used */
  gst_object_unref (appsrc1);
  gst_object_unref (appsrc2);
  gst_multi_appsrc_remove_source_id (GST_MULTI_APPSRC_CAST (multiappsrc),
      source_id1);
  gst_multi_appsrc_remove_source_id (GST_MULTI_APPSRC_CAST (multiappsrc),
      source_id2);
  gst_object_unref (multiappsrc);
  g_free (source_id1);
  g_free (source_id2);
  gst_structure_free (result1);
  gst_structure_free (result2);
  gst_structure_free (smart_prop);
}

GST_END_TEST;

GST_START_TEST (test_smart_properties_before_create)
{
  GstElement *multiappsrc;
  GstElement *appsrc1, *appsrc2;
  GstStructure *smart_prop;
  GstStructure *result1 = NULL, *result2 = NULL;
  gchar *source_id1 = NULL, *source_id2 = NULL;

  smart_prop = gst_structure_new ("smart-properties",
      "test", G_TYPE_INT, 10, NULL);

  /* step 1. set smart-properties after creating appsrc element */

  multiappsrc =
      gst_element_make_from_uri (GST_URI_SRC, "multiappsrc://", "source", NULL);

  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id1);
  fail_unless (source_id1, "failed to create 1st source");

  g_signal_emit_by_name (multiappsrc, "get-appsrc", source_id1, &appsrc1);
  fail_unless (appsrc1, "failed to get appsrc1");

  g_signal_emit_by_name (multiappsrc, "add-source-id", NULL, &source_id2);
  fail_unless (source_id2, "failed to create 2nd source");

  g_signal_emit_by_name (multiappsrc, "get-appsrc", source_id2, &appsrc2);
  fail_unless (appsrc2, "failed to get appsrc2");

  g_object_set (multiappsrc, "smart-properties", smart_prop, NULL);

  g_object_get (appsrc1, "smart-properties", &result1, NULL);
  g_object_get (appsrc2, "smart-properties", &result2, NULL);

  fail_unless (gst_structure_is_equal (smart_prop, result1),
      "step2:smart-properties does not propagate to appsrc.");
  fail_unless (gst_structure_is_equal (smart_prop, result2),
      "step2:smart-properties does not propagate to appsrc.");

  gst_object_unref (appsrc1);
  gst_object_unref (appsrc2);
  gst_multi_appsrc_remove_source_id (GST_MULTI_APPSRC_CAST (multiappsrc),
      source_id1);
  gst_multi_appsrc_remove_source_id (GST_MULTI_APPSRC_CAST (multiappsrc),
      source_id2);
  gst_object_unref (multiappsrc);
  g_free (source_id1);
  g_free (source_id2);
  gst_structure_free (result1);
  gst_structure_free (result2);
  gst_structure_free (smart_prop);
}

GST_END_TEST;

GST_START_TEST (test_smart_properties_runtime_add)
{
  GstElement *multiappsrc, *pipe;
  GstStructure *smart_prop;
  GstStructure *result1 = NULL, *result2 = NULL;
  GstElement *appsrc1, *appsrc2;
  GstBus *bus;
  GstMessage *msg;
  gchar *source_id1 = NULL, *source_id2 = NULL;

  smart_prop = gst_structure_new ("smart-properties",
      "test", G_TYPE_INT, 10, NULL);

  pipe = gst_pipeline_new ("pipeline");
  multiappsrc = gst_check_setup_element ("multiappsrc");
  g_object_set (multiappsrc, "smart-properties", smart_prop, NULL);
  g_signal_connect (multiappsrc, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (multiappsrc, "pad-removed",
      G_CALLBACK (pad_removed_cb), pipe);

  fail_unless (gst_bin_add (GST_BIN (pipe), multiappsrc));

  source_id1 =
      gst_multi_appsrc_add_source_id (GST_MULTI_APPSRC (multiappsrc), NULL);
  fail_unless (source_id1);

  g_signal_emit_by_name (multiappsrc, "get-appsrc", source_id1, &appsrc1);
  fail_unless (appsrc1, "failed to get appsrc1");

  g_object_get (appsrc1, "smart-properties", &result1, NULL);
  fail_unless (gst_structure_is_equal (smart_prop, result1),
      "step1:smart-properties does not propagate to appsrc.");

  gst_structure_free (result1);

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer to source id %s", source_id1);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  /* create new appsrc */
  source_id2 =
      gst_multi_appsrc_add_source_id (GST_MULTI_APPSRC (multiappsrc), NULL);
  fail_unless (source_id2);

  g_signal_emit_by_name (multiappsrc, "get-appsrc", source_id2, &appsrc2);
  fail_unless (appsrc2, "failed to get appsrc2");

  g_object_get (appsrc1, "smart-properties", &result1, NULL);
  g_object_get (appsrc2, "smart-properties", &result2, NULL);

  fail_unless (gst_structure_is_equal (smart_prop, result1),
      "step2:smart-properties does not propagate to appsrc.");
  fail_unless (gst_structure_is_equal (smart_prop, result2),
      "step2:smart-properties does not propagate to appsrc.");

  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id1, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer to source id %s", source_id1);
  fail_unless (gst_multi_appsrc_push_buffer (GST_MULTI_APPSRC (multiappsrc),
          source_id2, gst_buffer_new_and_alloc (4)) == GST_FLOW_OK,
      "fail to push buffer to source id %s", source_id2);

  bus = gst_element_get_bus (pipe);
  fail_if (bus == NULL);

  gst_multi_appsrc_end_of_stream (GST_MULTI_APPSRC (multiappsrc), NULL);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  gst_message_unref (msg);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (bus);

  gst_object_unref (pipe);
  g_free (source_id1);
  g_free (source_id2);
  gst_object_unref (appsrc1);
  gst_object_unref (appsrc2);
  gst_structure_free (result1);
  gst_structure_free (result2);
  gst_structure_free (smart_prop);
}

GST_END_TEST;


static Suite *
multiappsrc_suite (void)
{
  Suite *s = suite_create ("multiappsrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_uri_interface);
  tcase_add_test (tc_chain, test_multiappsrc_add_remove);
  tcase_add_test (tc_chain, test_multiappsrc_send_seek);
  tcase_add_test (tc_chain, test_multiappsrc_push_discont);
  tcase_add_test (tc_chain, test_multiappsrc_push_discont_sample);
  tcase_add_test (tc_chain, test_multiappsrc_pull_model_streaming);
  tcase_add_test (tc_chain, test_multiappsrc_pull_model_discont_streaming);
  tcase_add_test (tc_chain, test_smart_properties_before_create);
  tcase_add_test (tc_chain, test_smart_properties_after_create);
  tcase_add_test (tc_chain, test_smart_properties_runtime_add);

  return s;
}

GST_CHECK_MAIN (multiappsrc);
