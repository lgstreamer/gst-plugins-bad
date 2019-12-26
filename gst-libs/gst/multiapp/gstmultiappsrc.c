/* GStreamer Multiple App Sources element
 *
 * Copyright (C) 2014 LG Electronics, Inc.
 *  Author : Wonchul Lee <wonchul86.lee@lge.com>
 *           Hoonhee Lee <hoonhee.lee@lge.com>
 *           Myoungsun Lee <mysunny.lee@lge.com>
 *           Jeongseok Kim <jeongseok.kim@lge.com>
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

/**
 * SECTION:element-multiappsrc
 *
 * MultiAppSrc provides multiple appsrc elements inside a single source bin
 * for handling separated audio, video and text streams.
 *
 * <refsect2>
 * <title>Usage</title>
 * <para>
 * A multiappsrc element is created by pipeline based on "multiappsrc://" protocol URI.
 *
 * MultiAppSrc is #GstBin. An appsrc element is created by add-source-id signal action
 * to multiappsrc.
 *
 * MultiAppSrc supports adding or removing appsrc during PAUSED or PLAYING state.
 * Basically, every child appsrc will have identical "stream-type" and "format" properties,
 * It's possible that, however, setting detailed properties to an appsrc, by getting
 * a specific appsrc by calling _get_appsrc() method.
 *
 * If application want to switch track, _push_discont_buffer/sample() methods can
 * be used. The methods trigger changing state of an child appsrc from PLAYING/PAUSED
 * to READY. Then, internal new ghospad will be re-linked to appsrc and exposed.
 * After then, the appsrc's state will be restored.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * app->playbin = gst_element_factory_make ("playbin", NULL);
 * g_object_set (app->playbin, "uri", "multiappsrc://", NULL);
 * g_signal_connect (app->playbin, "deep-notify::source",
 *     (GCallback) found_source, app);
 * ]|
 * Create multiappsrc element in pipeline and watch source notify.
 *
 * |[
 * gchar *source_id = gst_multi_appsrc_add_source_id (multiappsrc, "thenameofappsrc");
 * ]|
 * Create internal appsrc element by add-source-id signal action, or a function.
 *
 * |[
 * g_signal_connect (multiappsrc, "need-data", G_CALLBACK (start_feed), app);
 * g_signal_connect (multiappsrc, "enough-data", G_CALLBACK (stop_feed), app);
 * g_signal_connect (multiappsrc, "seek-data", G_CALLBACK (seek_feed), app);
 * ]|
 * Like #GstAppSrc, #GstMultiAppSrc provides three types of signals.
 * Each signal has source_id as information on source classification.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gstmultiappsrc.h"
#include "gst/gst-i18n-plugin.h"

#define MULTI_APPSRC_LOCK(multiappsrc) G_STMT_START { \
    GST_TRACE_OBJECT (multiappsrc,  \
		    "locking from thread %p", \
		    g_thread_self ());  \
    g_mutex_lock (&GST_MULTI_APPSRC_CAST(multiappsrc)->priv->lock); \
    GST_TRACE_OBJECT (multiappsrc,  \
		    "locked from thread %p",  \
		    g_thread_self ());  \
} G_STMT_END

#define MULTI_APPSRC_UNLOCK(multiappsrc) G_STMT_START { \
    GST_TRACE_OBJECT (multiappsrc,  \
		    "unlocking from thread %p", \
		    g_thread_self ());  \
    g_mutex_unlock (&GST_MULTI_APPSRC_CAST(multiappsrc)->priv->lock); \
} G_STMT_END

#define HASH_LOCK(multiappsrc) G_STMT_START { \
    GST_TRACE_OBJECT (multiappsrc,  \
		    "hash locking from thread %p",  \
		    g_thread_self ());  \
    g_rec_mutex_lock (&GST_MULTI_APPSRC_CAST(multiappsrc)->priv->hash_lock);  \
    GST_TRACE_OBJECT (multiappsrc,  \
		    "hash locked from thread %p", \
		    g_thread_self ());  \
} G_STMT_END

#define HASH_UNLOCK(multiappsrc) G_STMT_START { \
    GST_TRACE_OBJECT (multiappsrc,  \
		    "hash unlocking from thread %p",  \
		    g_thread_self ());  \
    g_rec_mutex_unlock (&GST_MULTI_APPSRC_CAST(multiappsrc)->priv->hash_lock);  \
} G_STMT_END

struct _GstMultiAppSrcPrivate
{
  GstBus *bus;
  gchar *uri;
  gboolean do_seek;

  /* main lock */
  GMutex lock;
  GCond cond;

  /* properties, protected by main lock */
  gboolean emit_signals;
  GstAppStreamType stream_type;
  GstFormat format;

  gboolean running;             /* indicates state of multiappsrc */

  GRecMutex hash_lock;
  GHashTable *appsrc_info_id_pairs;     /* table of child appsrc info */

  /* task */
  GstTask *reconfigure_task;
  GRecMutex reconfigure_lock;

  /* callback */
  GstMultiAppSrcCallbacks callbacks;
  gpointer user_data;
  GDestroyNotify notify;

  GstStructure *smart_prop;
};

GST_DEBUG_CATEGORY_STATIC (multi_appsrc_debug);
#define GST_CAT_DEFAULT multi_appsrc_debug

#define parent_class gst_multi_appsrc_parent_class

#define DEFAULT_PROP_EMIT_SIGNALS TRUE
#define DEFAULT_PROP_STREAM_TYPE GST_APP_STREAM_TYPE_STREAM
#define DEFAULT_PROP_FORMAT GST_FORMAT_BYTES

enum
{
  PROP_0,
  PROP_N_SRC,
  PROP_EMIT_SIGNALS,
  PROP_STREAM_TYPE,
  PROP_FORMAT,
  PROP_SMART_PROPERTIES,
  PROP_LAST
};

enum
{
  /* actions */
  SIGNAL_ADD_SOURCE_ID,
  SIGNAL_REMOVE_SOURCE_ID,
  SIGNAL_GET_APPSRC,
  SIGNAL_END_OF_STREAM,
  SIGNAL_PUSH_BUFFER,
  SIGNAL_PUSH_SAMPLE,
  SIGNAL_PUSH_DISCONT_BUFFER,
  SIGNAL_PUSH_DISCONT_SAMPLE,
  /* signals */
  SIGNAL_NEED_DATA,
  SIGNAL_ENOUGH_DATA,
  SIGNAL_SEEK_DATA,
  SIGNAL_SOURCE_ADDED,
  LAST_SIGNAL
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static guint gst_multi_appsrc_signals[LAST_SIGNAL] = { 0 };

static void gst_multi_appsrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_multi_appsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_appsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_multi_appsrc_dispose (GObject * self);
static void gst_multi_appsrc_finalize (GObject * self);

static GstStateChangeReturn gst_multi_appsrc_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_multi_appsrc_element_send_event (GstElement * element,
    GstEvent * event);

static guint gst_multi_appsrc_get_num_source (GstMultiAppSrc * multiappsrc);

static GstBusSyncReply gst_multi_appsrc_bus_handler (GstBus * bus,
    GstMessage * message, GstBin * bin);
static void gst_multi_appsrc_reconfigure_loop (GstMultiAppSrc * multiappsrc);
static GstPadProbeReturn srcpad_event_probe (GstPad * pad, GstPadProbeInfo *
    info, gpointer user_data);
static GstPadProbeReturn srcpad_block_probe (GstPad * pad, GstPadProbeInfo *
    info, gpointer user_data);

static void dispose_reconfigure_loop (GstMultiAppSrc * multiappsrc);
static void remove_sources (GstMultiAppSrc * multiappsrc);

static GstFlowReturn gst_multi_appsrc_push_buffer_action (GstMultiAppSrc *
    multiappsrc, gchar * source_id, GstBuffer * buffer);
static GstFlowReturn gst_multi_appsrc_push_discont_buffer_action (GstMultiAppSrc
    * multiappsrc, gchar * source_id, GstBuffer * buffer);

typedef struct _ChildAppSrcInfo ChildAppSrcInfo;
typedef struct _OutputSlot OutputSlot;

struct _ChildAppSrcInfo
{
  gchar *source_id;             /* unique id for the AppSrc */

  GstElement *multiappsrc;      /* parent MultiAppSrc */
  GstElement *appsrc;

  OutputSlot *active_slot;      /* currently activated OutputSlot */
  OutputSlot *pending_slot;     /* the slot to be activated */

  GQueue *pending_data;         /* pending buffer/sample/event to be passed
                                 * through pending slot. protected by queue_lock */
  /* pending events (TAG, CUSTOM_BOTH, CUSTOM_DOWNSTREAM) to be
   * pushed in the data stream */
  GList *pending_events;
  gboolean have_events;
  guint block_id;

  GMutex queue_lock;
  GCond cond;

  gboolean do_reconfigure;      /* protected by queue_lock */
  gboolean reconfigure_posted;
};

struct _OutputSlot
{
  ChildAppSrcInfo *linked_info; /* ChildAppSrcInfo which belongs to.
                                 * Don't modify this linked_info by OutputSlot */
  GstPad *srcpad;               /* ghostpad (linked or to be linked) of appsrc's srcpad */
};

#define CHILD_APPSRC_INFO_CAST(obj) ((ChildAppSrcInfo *)obj)

#define QUEUE_LOCK(info) G_STMT_START { \
    GST_TRACE_OBJECT ((info)->appsrc, \
		    "queue locking from thread %p", \
		    g_thread_self ());  \
    g_mutex_lock (&CHILD_APPSRC_INFO_CAST(info)->queue_lock); \
    GST_TRACE_OBJECT ((info)->appsrc, \
		    "queue locked from thread %p",  \
		    g_thread_self ());  \
} G_STMT_END

#define QUEUE_UNLOCK(info) G_STMT_START { \
    GST_TRACE_OBJECT ((info)->appsrc, \
		    "queue unlocking from thread %p", \
		    g_thread_self ());  \
    g_mutex_unlock (&CHILD_APPSRC_INFO_CAST(info)->queue_lock); \
} G_STMT_END

static void gst_multi_appsrc_set_stream_type_internal (GstMultiAppSrc *
    multiappsrc, ChildAppSrcInfo * info);
static void gst_multi_appsrc_set_format_internal (GstMultiAppSrc *
    multiappsrc, ChildAppSrcInfo * info);

static void appsrc_need_data_cb (GstAppSrc * src, guint size,
    gpointer user_data);
static void appsrc_enough_data_cb (GstAppSrc * src, gpointer user_data);
static gboolean appsrc_seek_data_cb (GstAppSrc * src, guint64 offset,
    gpointer user_data);

static void
free_output_slot (OutputSlot * slot)
{
  GstObject *bin = gst_pad_get_parent (slot->srcpad);

  if (bin) {
    GST_LOG_OBJECT (bin, "srcpad was removed");

    gst_pad_set_active (slot->srcpad, FALSE);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (slot->srcpad), NULL);
    gst_element_remove_pad (GST_ELEMENT_CAST (bin), slot->srcpad);
    gst_object_unref (bin);
  } else {
    GST_LOG_OBJECT (slot->srcpad, "srcpad has no parent");
    gst_pad_set_active (slot->srcpad, FALSE);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (slot->srcpad), NULL);
    gst_object_unref (slot->srcpad);
  }

  g_free (slot);
}

static void
free_child_appsrc_info (ChildAppSrcInfo * info)
{
  QUEUE_LOCK (info);
  if (info->active_slot) {
    free_output_slot (info->active_slot);
    info->active_slot = NULL;
  }

  if (info->pending_slot) {
    free_output_slot (info->pending_slot);
    info->pending_slot = NULL;
  }

  if (info->appsrc) {
    GstObject *bin = gst_element_get_parent (info->appsrc);
    gst_element_set_state (info->appsrc, GST_STATE_NULL);
    if (bin) {
      GST_LOG_OBJECT (bin, "internal appsrc was removed");
      gst_bin_remove (GST_BIN_CAST (bin), info->appsrc);
      gst_object_unref (bin);
    } else {
      gst_object_unref (info->appsrc);
    }
    info->appsrc = NULL;
  }

  if (info->pending_events) {
    g_list_foreach (info->pending_events, (GFunc) gst_event_unref, NULL);
    g_list_free (info->pending_events);
  }

  while (!g_queue_is_empty (info->pending_data)) {
    GstMiniObject *obj = g_queue_pop_head (info->pending_data);
    if (obj)
      gst_mini_object_unref (obj);
  }

  QUEUE_UNLOCK (info);

  g_queue_free (info->pending_data);
  g_mutex_clear (&info->queue_lock);
  g_cond_clear (&info->cond);
  gst_object_unref (info->multiappsrc);
  g_free (info->source_id);
  g_free (info);
}

/* must be called with QUEUE_LOCK */
static GstFlowReturn
wait_reconfigure (ChildAppSrcInfo * info)
{
  g_assert (info);
  g_assert (info->appsrc);

  if (info->do_reconfigure) {
    if (!info->pending_slot) {
      /* if we are in here, this child appsrc will be removed */
      GST_DEBUG_OBJECT (info->appsrc, "do not wait for removing child appsrc");

      return GST_FLOW_EOS;
    } else {
      GST_DEBUG_OBJECT (info->appsrc, "wait reconfigure");
      g_cond_wait (&info->cond, &info->queue_lock);
    }
  }

  return GST_FLOW_OK;
}

G_DEFINE_TYPE_WITH_CODE (GstMultiAppSrc, gst_multi_appsrc, GST_TYPE_BIN,
    G_ADD_PRIVATE (GstMultiAppSrc)
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_multi_appsrc_uri_handler_init));


static void
gst_multi_appsrc_class_init (GstMultiAppSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_multi_appsrc_set_property;
  gobject_class->get_property = gst_multi_appsrc_get_property;
  gobject_class->dispose = gst_multi_appsrc_dispose;
  gobject_class->finalize = gst_multi_appsrc_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  /**
   * GstMultiAppSrc:n-src
   *
   * Get the total number of available streams.
   */
  g_object_class_install_property (gobject_class, PROP_N_SRC,
      g_param_spec_int ("n-source", "Number Source",
          "Total number of source streams", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiAppSrc::emit-signals:
   *
   * Make multiappsrc emit the "need-data", "enough-data" and "seek-data" signals.
   * This option is by default enabled for backwards compatibility reasons but
   * can disabled when needed because signal emission is expensive.
   */
  g_object_class_install_property (gobject_class, PROP_EMIT_SIGNALS,
      g_param_spec_boolean ("emit-signals", "Emit signals",
          "Emit need-data, enough-data and seek-data signals",
          DEFAULT_PROP_EMIT_SIGNALS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  /**
  * GstMultiAppSrc::stream-type:
  *
  * Set stream-type of child appsrc's. Defaultly, all child appsrc will inherit
  * stream-type of MultiAppSrc. To set different stream-type of each child appsrc,
  * caller have to access to the appsrc directly by using _get_appsrc() method.
  * See also "GstAppSrc::stream-type"
  */
  g_object_class_install_property (gobject_class, PROP_STREAM_TYPE,
      g_param_spec_enum ("stream-type", "Stream Type",
          "the type of the stream", GST_TYPE_APP_STREAM_TYPE,
          DEFAULT_PROP_STREAM_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
  * GstMultiAppSrc::format:
  *
  * Set format of child appsrc's. Defaultly, all child appsrc will inherit
  * format of MultiAppSrc. To set different format of each child appsrc,
  * caller have to access to the appsrc directly by using _get_appsrc() method.
  * See also "GstAppSrc::format"
  */
  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_enum ("format", "Format",
          "The format of the segment events and seek", GST_TYPE_FORMAT,
          DEFAULT_PROP_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SMART_PROPERTIES,
      g_param_spec_boxed ("smart-properties", "Smart Properties",
          "Hold various property values for reply custom query",
          GST_TYPE_STRUCTURE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiAppSrc::add-source-id
   * @multiappsrc: a #GstMultiAppSrc
   * @name : name of appsrc element
   *
   * Action signal to create an internal appsrc element.
   * This signal should be emitted at least one time before changing state READY to PAUSED.
   * So, the application emit this signal, at least one time,
   * as soon as receiving source-setup signal from pipeline.
   *
   * Also, it is allowed to emit this signal in PAUSED or PLAYING state.
   * In that case, @multiappsrc will configure new appsrc with ghostpad.
   * Adjacent downstream element (or parent bin/pipeline) must keep watch over
   * the "pad-added" signal, and newly added ghostpad need to be suitably handled.
   *
   * Application should set unique @name to create new appsrc element.
   * If not, @multiappsrc will not create new appsrc element with return %NULL
   *
   * Returns: source id if the add-source-id succeeded (%NULL if failed).
   */
  gst_multi_appsrc_signals[SIGNAL_ADD_SOURCE_ID] =
      g_signal_new ("add-source-id", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, add_source_id), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_STRING, 1, G_TYPE_STRING);

  /**
   * GstMultiAppSrc::remove-source-id
   * @multiappsrc: a #GstMultiAppSrc
   * @source_id : source id
   *
   * Action signal to remove an internal appsrc element.
   * If @multiappsrc has multiple child appsrc, the application is allowed to
   * emit this signal in PAUSED or PLAYING state. Otherwise, @multiappsrc
   * will return with %FALSE
   *
   * Returns: %TRUE if the remove-source-id succeeded (%FALSE if failed).
   */
  gst_multi_appsrc_signals[SIGNAL_REMOVE_SOURCE_ID] =
      g_signal_new ("remove-source-id", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, remove_source_id), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);

  /**
   * GstMultiAppSrc::get-appsrc
   * @multiappsrc: a #GstMultiAppSrc
   * @source_id : source id
   *
   * Action signal to get internal appsrc reference.
   * The appsrc should be unref after using.
   * If @source_id was invalid, @multiappsrc will return %NULL.
   *
   * Returns: appsrc element with increased ref count. (%NULL if failed).
   */
  gst_multi_appsrc_signals[SIGNAL_GET_APPSRC] =
      g_signal_new ("get-appsrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, get_appsrc), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_APP_SRC, 1, G_TYPE_STRING);

  /**
   * GstMultAppSrc::need-data:
   * @multiappsrc: the multiappsrc element that emitted the signal
   * @source_id: the source id that originally emitted the signal
   * @length: the amount of bytes needed.
   *
   * Signal that the source needs more data. In the callback or from another
   * thread you should call push-buffer or end-of-stream.
   *
   * @length is just a hint and when it is set to -1, any number of bytes can be
   * pushed into @appsrc.
   *
   * You can call push-buffer multiple times until the enough-data signal is
   * fired.
   */
  gst_multi_appsrc_signals[SIGNAL_NEED_DATA] =
      g_signal_new ("need-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, need_data),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2, G_TYPE_STRING,
      G_TYPE_UINT);

  /**
   * GstMultiAppSrc::enough-data:
   * @multiappsrc: the multiappsrc element that emitted the signal
   * @source_id: the source id that originally emitted the signal
   *
   * Signal that the source has enough data. It is recommended that the
   * application stops calling push-buffer until the need-data signal is
   * emitted again to avoid excessive buffer queueing.
   */
  gst_multi_appsrc_signals[SIGNAL_ENOUGH_DATA] =
      g_signal_new ("enough-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, enough_data),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * GstMultiAppSrc::seek-data:
   * @multiappsrc: the multiappsrc element that emitted the signal
   * @source_id: the source id that originally emitted the signal
   * @offset: the offset to seek to
   *
   * Seek to the given offset. The next push-buffer should produce buffers from
   * the new @offset.
   * This callback is only called for seekable stream types.
   *
   * Returns: %TRUE if the seek succeeded.
   */
  gst_multi_appsrc_signals[SIGNAL_SEEK_DATA] =
      g_signal_new ("seek-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, seek_data),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 2,
      G_TYPE_STRING, G_TYPE_UINT64);

  /**
    * GstMultiAppSrc::end-of-stream:
    * @multiappsrc: a #GstMultiAppSrc
    * @source_id : source id (nullable)
    *
    * Notify appsrc corresponding to @source_id that no more buffer are available.
    * if @source_id was not specified, end-of-stream will be notified to all
    * child appsrc's.
    */
  gst_multi_appsrc_signals[SIGNAL_END_OF_STREAM] =
      g_signal_new ("end-of-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiAppSrcClass, end_of_stream), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_FLOW_RETURN, 1, G_TYPE_STRING);

  /**
   * GstMultiAppSrc::push-buffer:
   * @multiappsrc: the multiappsrc element that emitted the signal
   * @source_id: the source id that originally emitted the signal
   * @buffer: a buffer to push
   *
   * Push a @buffer to appsrc corresponding to @source_id. This function does
   * not take ownership of the buffer, so the buffer needs to be unreffed after
   * calling this function.
   * See also GstAppSrc::push-buffer:
   */
  gst_multi_appsrc_signals[SIGNAL_PUSH_BUFFER] =
      g_signal_new ("push-buffer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiAppSrcClass,
          push_buffer), NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_FLOW_RETURN, 2, G_TYPE_STRING, GST_TYPE_BUFFER);


  /**
   * GstMultiAppSrc::push-sample:
   * @multiappsrc: the multiappsrc element that emitted the signal
   * @source_id: the source id that originally emitted the signal
   * @sample: a sample to push
   *
   * Push a @sample to appsrc corresponding to @source_id. This function does
   * not take ownership of the sample, so the sample needs to be unreffed after
   * calling this function.
   * See also GstAppSrc::push-sample:
   */
  gst_multi_appsrc_signals[SIGNAL_PUSH_SAMPLE] =
      g_signal_new ("push-sample", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiAppSrcClass,
          push_sample), NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_FLOW_RETURN, 2, G_TYPE_STRING, GST_TYPE_SAMPLE);

  /**
    * GstMultiAppSrc::push-discont-buffer:
    * @multiappsrc: a #GstMultiAppSrc
    * @source_id : source id
    * @buffer: a buffer to be push
    *
    * Push a @buffer to appsrc likewise push-buffer. @multiappsrc internally,
    * the child appsrc will be re-configured. That is, appsrc will push
    * startup events such as stream-start, caps, and segment to new ghostpad.
    * then, @buffer will also be pushed to new ghostpad. Previously used ghostpad
    * will be removed.
    *
    * Adjacent downstream element (or parent bin/pipeline) must keep watch over
    * the "pad-added" and "pad-removed" signal. @multiappsrc guarantee that
    * firing "pad-added" signal first, and then "pad-removed"
    *
    * See also GstUriSourceBin, how it handles pads of adaptive-demuxer.
    */
  gst_multi_appsrc_signals[SIGNAL_PUSH_DISCONT_BUFFER] =
      g_signal_new ("push-discont-buffer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiAppSrcClass,
          push_discont_buffer), NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_FLOW_RETURN, 2, G_TYPE_STRING, GST_TYPE_BUFFER);

  /**
    * GstMultiAppSrc::push-discont-sample:
    * @multiappsrc: a #GstMultiAppSrc
    * @id : source id
    * @sample: a sample to be push
    *
    * Push a @sample to appsrc likewise push-sample. @multiappsrc internally,
    * the child appsrc will be re-configured. That is, appsrc will push
    * startup events such as stream-start, caps, and segment to new ghostpad.
    * then, @sample will also be pushed to new ghostpad. Previously used ghostpad
    * will be removed.
    *
    * Adjacent downstream element (or parent bin/pipeline) must keep watch over
    * the "pad-added" and "pad-removed" signal. @multiappsrc guarantee that
    * firing "pad-added" signal first, and then "pad-removed"
    *
    * See also GstUriSourceBin, how it handles pads of adaptive-demuxer.
    */
  gst_multi_appsrc_signals[SIGNAL_PUSH_DISCONT_SAMPLE] =
      g_signal_new ("push-discont-sample", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstMultiAppSrcClass,
          push_discont_sample), NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_FLOW_RETURN, 2, G_TYPE_STRING, GST_TYPE_SAMPLE);

  gst_multi_appsrc_signals[SIGNAL_SOURCE_ADDED] =
      g_signal_new ("source-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiAppSrcClass, source_added),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2, G_TYPE_STRING,
      G_TYPE_STRING);

  klass->add_source_id = gst_multi_appsrc_add_source_id;
  klass->remove_source_id = gst_multi_appsrc_remove_source_id;
  klass->get_appsrc = gst_multi_appsrc_get_appsrc;

  klass->push_buffer = gst_multi_appsrc_push_buffer_action;
  klass->end_of_stream = gst_multi_appsrc_end_of_stream;
  klass->push_sample = gst_multi_appsrc_push_sample;

  klass->push_discont_buffer = gst_multi_appsrc_push_discont_buffer_action;
  klass->push_discont_sample = gst_multi_appsrc_push_discont_sample;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_multi_appsrc_change_state);

  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_multi_appsrc_element_send_event);

  gst_element_class_set_static_metadata (gstelement_class,
      "Multiple Appsrc", "Source/Bin",
      "Create multiple appsrc elements in a single srcbin",
      "Wonchul Lee <wonchul86.lee@lge.com>, "
      "Justin Kim <justin.kim@collabora.com>, "
      "Seungha Yang <sh.yang@lge.com>");

  GST_DEBUG_CATEGORY_INIT (multi_appsrc_debug, "multiappsrc", 0,
      "Multiple App Sources Bin");
}

static void
gst_multi_appsrc_init (GstMultiAppSrc * bin)
{
  GstMultiAppSrcPrivate *priv;

  priv = bin->priv = gst_multi_appsrc_get_instance_private (bin);

  g_mutex_init (&priv->lock);
  g_rec_mutex_init (&priv->hash_lock);
  g_cond_init (&priv->cond);

  priv->appsrc_info_id_pairs =
      g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free,
      (GDestroyNotify) free_child_appsrc_info);

  priv->emit_signals = DEFAULT_PROP_EMIT_SIGNALS;
  priv->stream_type = DEFAULT_PROP_STREAM_TYPE;
  priv->format = DEFAULT_PROP_FORMAT;
  priv->running = FALSE;
  priv->do_seek = FALSE;
  priv->smart_prop = NULL;

  priv->bus = gst_bus_new ();
  gst_bus_set_sync_handler (priv->bus, (GstBusSyncHandler)
      gst_multi_appsrc_bus_handler, bin, NULL);
  g_rec_mutex_init (&priv->reconfigure_lock);
  priv->reconfigure_task = gst_task_new ((GstTaskFunction)
      gst_multi_appsrc_reconfigure_loop, bin, NULL);
  gst_task_set_lock (priv->reconfigure_task, &priv->reconfigure_lock);

  GST_OBJECT_FLAG_SET (bin, GST_ELEMENT_FLAG_SOURCE);
}

/* must be called with MULTI_APPSRC_LOCK */
static gboolean
set_smart_properties (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstStructure *smart_prop = (GstStructure *) user_data;

  gst_structure_id_set_value (smart_prop, field_id, value);
  return TRUE;
}

/* must be called with MULTI_APPSRC_LOCK */
static void
apply_smart_properties (GstMultiAppSrc * multiappsrc, ChildAppSrcInfo * info)
{
  GstMultiAppSrcPrivate *priv;

  g_return_if_fail (info);
  g_return_if_fail (info->appsrc);

  priv = multiappsrc->priv;

  g_object_set (GST_ELEMENT_CAST (info->appsrc), "smart-properties",
      priv->smart_prop, NULL);
}

static void
gst_multi_appsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC (object);

  switch (prop_id) {
    case PROP_EMIT_SIGNALS:
      gst_multi_appsrc_set_emit_signals (multiappsrc,
          g_value_get_boolean (value));
      break;
    case PROP_STREAM_TYPE:
      gst_multi_appsrc_set_stream_type (multiappsrc, g_value_get_enum (value));
      break;
    case PROP_FORMAT:
      gst_multi_appsrc_set_format (multiappsrc, g_value_get_enum (value));
      break;
    case PROP_SMART_PROPERTIES:
    {
      const GstStructure *s = gst_value_get_structure (value);
      GstMultiAppSrcPrivate *priv;
      GHashTableIter iter;
      gpointer key, data;

      priv = multiappsrc->priv;

      MULTI_APPSRC_LOCK (multiappsrc);
      if (priv->smart_prop)
        gst_structure_foreach (s, set_smart_properties, priv->smart_prop);
      else
        priv->smart_prop = gst_structure_copy (s);

      g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);

      while (g_hash_table_iter_next (&iter, &key, &data)) {
        apply_smart_properties (multiappsrc, data);
      }

      MULTI_APPSRC_UNLOCK (multiappsrc);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_appsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC (object);

  switch (prop_id) {
    case PROP_N_SRC:
      g_value_set_int (value, gst_multi_appsrc_get_num_source (multiappsrc));
      break;
    case PROP_EMIT_SIGNALS:
      g_value_set_boolean (value,
          gst_multi_appsrc_get_emit_signals (multiappsrc));
      break;
    case PROP_STREAM_TYPE:
      g_value_set_enum (value, gst_multi_appsrc_get_stream_type (multiappsrc));
      break;
    case PROP_FORMAT:
      g_value_set_enum (value, gst_multi_appsrc_get_format (multiappsrc));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_appsrc_dispose (GObject * self)
{
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC_CAST (self);
  GstMultiAppSrcPrivate *priv = multiappsrc->priv;

  GST_OBJECT_LOCK (multiappsrc);

  remove_sources (multiappsrc);

  if (priv->notify) {
    priv->notify (priv->user_data);
    priv->notify = NULL;
  }

  if (priv->bus) {
    gst_object_unref (priv->bus);
    priv->bus = NULL;
  }

  GST_OBJECT_UNLOCK (multiappsrc);

  G_OBJECT_CLASS (parent_class)->dispose (self);
}

static void
gst_multi_appsrc_finalize (GObject * self)
{
  GstMultiAppSrc *bin = GST_MULTI_APPSRC_CAST (self);
  GstMultiAppSrcPrivate *priv = bin->priv;

  g_cond_clear (&priv->cond);
  g_mutex_clear (&priv->lock);
  g_rec_mutex_clear (&priv->hash_lock);

  if (priv->appsrc_info_id_pairs != NULL) {
    g_hash_table_unref (priv->appsrc_info_id_pairs);
    priv->appsrc_info_id_pairs = NULL;
  }
  g_free (priv->uri);

  if (priv->smart_prop) {
    gst_structure_free (priv->smart_prop);
    priv->smart_prop = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (self);
}


static gboolean
gst_multi_appsrc_handle_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstPad *target = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (pad));
  gboolean res = FALSE;

  /* forward the query to the proxy target pad */
  if (target) {
    res = gst_pad_query (target, query);
    gst_object_unref (target);
  } else {
    /* if we are doing reconfigure, query caps should be forwarded to appsrc */
    OutputSlot *slot =
        g_object_get_data (G_OBJECT (pad), "multiappsrc.outputslot");

    if (slot && slot->linked_info && slot->linked_info->appsrc) {
      target = gst_element_get_static_pad (slot->linked_info->appsrc, "src");
      if (target) {
        res = gst_pad_query (target, query);
        gst_object_unref (target);
      }
    }
  }

  return res;
}

static gboolean
gst_multi_appsrc_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res = TRUE;
  GstPad *target;
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC (parent);
  GstMultiAppSrcPrivate *priv = multiappsrc->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GHashTableIter iter;
      gpointer key, value;

      MULTI_APPSRC_LOCK (multiappsrc);
      priv->do_seek = TRUE;
      MULTI_APPSRC_UNLOCK (multiappsrc);
      GST_DEBUG_OBJECT (pad, "Got event seek");

      //HASH_LOCK (multiappsrc);
      /* multiappsrc send a seek event for each of all linked appsrc elements. */
      g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        gboolean resend_eos = FALSE, do_forward = TRUE;
        ChildAppSrcInfo *info = CHILD_APPSRC_INFO_CAST (value);
        QUEUE_LOCK (info);
        if (info->do_reconfigure) {
          GST_DEBUG_OBJECT (info->appsrc, "doing reconfigure");
          if (!info->pending_slot) {
            GST_DEBUG_OBJECT (info->appsrc,
                "do not forward seek to removing slot");
            do_forward = FALSE;
          } else if (!info->reconfigure_posted) {
            GST_DEBUG_OBJECT (info->appsrc, "resend end-of-stream");
            resend_eos = TRUE;
          }
        }
        QUEUE_UNLOCK (info);

        if (do_forward) {
          GST_DEBUG_OBJECT (info->appsrc, "forward seek");
          res = gst_element_send_event (info->appsrc, gst_event_ref (event));
          GST_DEBUG_OBJECT (info->appsrc, "send seek event ret = %d", res);
        }

        if (resend_eos) {
          GST_DEBUG_OBJECT (info->appsrc, "resend eos");
          gst_app_src_end_of_stream (GST_APP_SRC (info->appsrc));
          GST_DEBUG_OBJECT (info->appsrc, "resend eos done");
        }
        GST_DEBUG_OBJECT (info->appsrc, "handle seek done for an appsrc");

      }
      //HASH_UNLOCK (multiappsrc);

      MULTI_APPSRC_LOCK (multiappsrc);
      priv->do_seek = FALSE;
      g_cond_broadcast (&priv->cond);
      MULTI_APPSRC_UNLOCK (multiappsrc);
      gst_event_unref (event);

      break;
    }
    default:
    {
      target = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (pad));
      if (!target) {
        gst_event_unref (event);
        break;
      }

      res = gst_pad_event_default (pad, parent, event);
      gst_object_unref (target);
      break;
    }
  }
  return res;
}

/* must be called with QUEUE_LOCK */
static void
expose_slot (GstMultiAppSrc * multiappsrc, ChildAppSrcInfo * info,
    gboolean is_pending)
{
  GstPadTemplate *pad_tmpl;
  GstPad *srcpad, *ghostpad;
  gchar *padname;
  OutputSlot *slot;

  GST_DEBUG_OBJECT (info->appsrc, "expose new output slot, is_pending (%d)",
      is_pending);

  if (is_pending && info->pending_slot) {
    /* we don't need to duplicated pending_slot */
    GST_DEBUG_OBJECT (info->appsrc, "we have a pending_slot already");
    return;
  }

  pad_tmpl = gst_static_pad_template_get (&src_template);
  srcpad = gst_element_get_static_pad (info->appsrc, "src");
  padname = g_strdup_printf ("src_%u", gst_util_seqnum_next ());

  if (!is_pending) {
    ghostpad = gst_ghost_pad_new_from_template (padname, srcpad, pad_tmpl);
  } else {
    ghostpad = gst_ghost_pad_new_no_target_from_template (padname, pad_tmpl);
    info->block_id = gst_pad_add_probe (ghostpad,
        GST_PAD_PROBE_TYPE_BLOCK_UPSTREAM, srcpad_block_probe, NULL, NULL);
  }

  slot = g_new0 (OutputSlot, 1);
  slot->linked_info = info;
  slot->srcpad = ghostpad;

  g_object_set_data_full (G_OBJECT (ghostpad), "multiappsrc.outputslot",
      slot, NULL);

  gst_pad_set_event_function (ghostpad, gst_multi_appsrc_handle_src_event);
  gst_pad_set_query_function (ghostpad, gst_multi_appsrc_handle_src_query);
  gst_pad_add_probe (ghostpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_EVENT_FLUSH,
      (GstPadProbeCallback) srcpad_event_probe, info, NULL);

  gst_pad_set_active (ghostpad, TRUE);

  if (!is_pending) {
    info->active_slot = slot;
  } else {
    info->pending_slot = slot;
  }

  gst_element_add_pad (GST_ELEMENT_CAST (multiappsrc), ghostpad);

  GST_DEBUG_OBJECT (ghostpad, "finish expose new ghostpad");

  gst_object_unref (srcpad);
  gst_object_unref (pad_tmpl);
  g_free (padname);
}

/* must be called with MULTI_APPSRC_LOCK */
static gboolean
setup_sources (GstMultiAppSrc * bin)
{
  gboolean ret = FALSE;

  GHashTableIter iter;
  gpointer key, value;

  GstMultiAppSrcPrivate *priv;

  priv = bin->priv;

  GST_DEBUG_OBJECT (bin, "trying to complete exposing all pads.");

  HASH_LOCK (bin);
  g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    /* There must exist at least one source info */
    ChildAppSrcInfo *info = CHILD_APPSRC_INFO_CAST (value);
    QUEUE_LOCK (info);
    expose_slot (bin, info, FALSE);
    QUEUE_UNLOCK (info);
    ret |= TRUE;
  }
  HASH_UNLOCK (bin);

  GST_DEBUG_OBJECT (bin, "all appsrc elements are added");
  gst_element_no_more_pads (GST_ELEMENT_CAST (bin));

  /* run reconfigure task */
  gst_task_start (priv->reconfigure_task);
  priv->running = TRUE;

  return ret;
}

static void
dispose_reconfigure_loop (GstMultiAppSrc * multiappsrc)
{
  GstMultiAppSrcPrivate *priv;

  priv = multiappsrc->priv;

  if (priv->reconfigure_task) {
    if (GST_TASK_STATE (priv->reconfigure_task) != GST_TASK_STOPPED) {
      GstMessage *msg;
      GstStructure *str;
      gst_task_stop (priv->reconfigure_task);

      str = gst_structure_new_empty ("source-drained");
      msg = gst_message_new_element (GST_OBJECT (multiappsrc), str);
      g_cond_signal (&priv->cond);

      GST_DEBUG_OBJECT (multiappsrc, "posting empty source-drained message: %"
          GST_PTR_FORMAT, msg);
      gst_bus_post (priv->bus, msg);
    }

    gst_task_join (priv->reconfigure_task);
    gst_object_unref (priv->reconfigure_task);
    g_rec_mutex_clear (&priv->reconfigure_lock);

    priv->reconfigure_task = NULL;
  }
}

static void
remove_sources (GstMultiAppSrc * multiappsrc)
{
  GstMultiAppSrcPrivate *priv;
  g_assert (GST_IS_MULTI_APPSRC (multiappsrc));

  priv = multiappsrc->priv;
  priv->running = FALSE;

  dispose_reconfigure_loop (multiappsrc);
  g_hash_table_remove_all (priv->appsrc_info_id_pairs);
}

/* must be called with QUEUE_LOCK */
static void
drain_pending_events (ChildAppSrcInfo * info)
{
  GList *pending_events = NULL, *tmp;

  /* push pending TAG, CUSTOM_BOTH, CUSTOM_DOWNSTREAM, and PROTECTION event */
  if (info->have_events) {
    pending_events = info->pending_events;
    info->pending_events = NULL;
    info->have_events = FALSE;
  }

  if (G_UNLIKELY (pending_events != NULL)) {
    GST_LOG_OBJECT (info->appsrc, "push pending events");
    for (tmp = pending_events; tmp; tmp = g_list_next (tmp)) {
      GstEvent *ev = (GstEvent *) tmp->data;
      gst_element_send_event (info->appsrc, ev);
    }
    g_list_free (pending_events);
  }
}

/* must be called with QUEUE_LOCK */
static GstFlowReturn
drain_pending_data (ChildAppSrcInfo * info)
{
  GstFlowReturn ret = GST_FLOW_OK;

  g_assert (info);
  g_assert (info->appsrc);

  drain_pending_events (info);

  while (!g_queue_is_empty (info->pending_data)) {
    GstMiniObject *obj = g_queue_pop_head (info->pending_data);
    if (GST_IS_BUFFER (obj)) {
      GST_TRACE_OBJECT (info->appsrc, "pushing pending buffer");
      ret = gst_app_src_push_buffer (GST_APP_SRC (info->appsrc),
          GST_BUFFER (obj));
    } else if (GST_IS_SAMPLE (obj)) {
      GST_TRACE_OBJECT (info->appsrc, "pushing pending sample");
      ret = gst_app_src_push_sample (GST_APP_SRC (info->appsrc),
          GST_SAMPLE (obj));
      /* for sample case, multiappsrc has ownership. we have to free it ourselves */
      gst_sample_unref (GST_SAMPLE (obj));
    } else if (GST_IS_EVENT (obj)) {
      GstEvent *event = GST_EVENT (obj);
      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
        gst_app_src_end_of_stream (GST_APP_SRC (info->appsrc));
        gst_event_unref (GST_EVENT (obj));
      } else {
        /* some other serialized event might be queued */
        gst_element_send_event (info->appsrc, GST_EVENT (obj));
      }
    } else if (obj) {
      GST_TRACE_OBJECT (info->appsrc, "unknown object detected. ignore");
      gst_mini_object_unref (obj);
    }
  }

  return ret;
}

static void
sync_internal_appsrc_state (gchar * source_id, ChildAppSrcInfo * info,
    gpointer data)
{
  gst_element_sync_state_with_parent (GST_ELEMENT (info->appsrc));
}

static GstStateChangeReturn
gst_multi_appsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMultiAppSrcPrivate *priv;
  GstMultiAppSrc *bin = GST_MULTI_APPSRC (element);

  priv = bin->priv;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      MULTI_APPSRC_LOCK (bin);
      setup_sources (bin);
      MULTI_APPSRC_UNLOCK (bin);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  GST_DEBUG_OBJECT (bin, "transition:%d return from parent class: %s",
      transition, gst_element_state_change_return_get_name (ret));

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      HASH_LOCK (bin);
      g_hash_table_foreach (priv->appsrc_info_id_pairs,
          (GHFunc) sync_internal_appsrc_state, NULL);
      HASH_UNLOCK (bin);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT (bin, "remove source elements");
      MULTI_APPSRC_LOCK (bin);
      remove_sources (bin);
      MULTI_APPSRC_UNLOCK (bin);
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (bin, "%s", gst_element_state_change_return_get_name (ret));

  return ret;
}

static gboolean
gst_multi_appsrc_element_send_event (GstElement * element, GstEvent * event)
{
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC_CAST (element);
  GstMultiAppSrcPrivate *priv = multiappsrc->priv;
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      /* default element_send_event handler of bin class will send seek event
       * to all appsrc's srcpad. Also, our srcpad event handler will
       * spread seek event to all appsrc's srcpad. It makes duplicated seek event.
       * To prevent this, send seek event to all appsrc directly */
      GHashTableIter iter;
      gpointer key, value;
      HASH_LOCK (multiappsrc);
      g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        ChildAppSrcInfo *info = (ChildAppSrcInfo *) value;
        if (info->appsrc)
          ret |= gst_element_send_event (info->appsrc, gst_event_ref (event));
      }
      HASH_UNLOCK (multiappsrc);
      gst_event_unref (event);

      return ret;
    }
    default:
      GST_DEBUG ("gst_multi_appsrc_send_event as DEFAULT");
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, send_event, (element,
          event), FALSE);
}

static guint
gst_multi_appsrc_get_num_source (GstMultiAppSrc * multiappsrc)
{
  GstMultiAppSrcPrivate *priv;
  guint ret = 0;

  priv = multiappsrc->priv;

  HASH_LOCK (multiappsrc);
  if (!priv->running) {
    ret = g_hash_table_size (priv->appsrc_info_id_pairs);
  } else {
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      ChildAppSrcInfo *info = (ChildAppSrcInfo *) value;
      if (info) {
        QUEUE_LOCK (info);
        if (info->do_reconfigure && !info->pending_slot) {
          GST_TRACE_OBJECT (multiappsrc, "ignore removing appsrc info");
        } else {
          ret++;
        }
        QUEUE_UNLOCK (info);
      }
    }
  }
  HASH_UNLOCK (multiappsrc);

  return ret;
}


static GstBusSyncReply
gst_multi_appsrc_bus_handler (GstBus * bus, GstMessage * message, GstBin * bin)
{
  const GstStructure *structure;
  /* only source-drained message wili be posted on this bus */
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT
      && gst_message_has_name (message, "source-drained")) {
    GST_DEBUG_OBJECT (bus, "Got source-drained message");

    structure = gst_message_get_structure (message);

    if (!structure || !gst_structure_has_field (structure, "source-id")) {
      GST_LOG_OBJECT (bus, "empty source-drained message will not be posted");
    } else {
      /* post message because application may want to get this message */
      gst_element_post_message (GST_ELEMENT_CAST (bin),
          gst_message_ref (message));
    }

    return GST_BUS_PASS;
  } else {
    gst_message_unref (message);
  }

  return GST_BUS_DROP;
}

static void
gst_multi_appsrc_reconfigure_loop (GstMultiAppSrc * multiappsrc)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info;
  const GstStructure *structure;
  const gchar *source_id;
  GstMessage *msg;

  priv = multiappsrc->priv;

  msg = gst_bus_timed_pop_filtered (priv->bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ELEMENT);

  GST_DEBUG_OBJECT (multiappsrc, "source-drained message: %" GST_PTR_FORMAT,
      msg);

  structure = gst_message_get_structure (msg);

  if (!structure || !gst_structure_has_field (structure, "source-id")) {
    GST_DEBUG_OBJECT (multiappsrc, "Got empty message");
    gst_message_unref (msg);
    return;
  }

  source_id = gst_structure_get_string (structure, "source-id");

  /* wait seek event handling if we got */
  MULTI_APPSRC_LOCK (multiappsrc);
  if (priv->do_seek) {
    GST_DEBUG_OBJECT (multiappsrc, "wait seeking");
    g_cond_wait (&priv->cond, &priv->lock);
    GST_DEBUG_OBJECT (multiappsrc, "continue with seeking done");
  }
  MULTI_APPSRC_UNLOCK (multiappsrc);

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);

  /* FIXME: is it possible ? */
  if (G_UNLIKELY (!info)) {
    HASH_UNLOCK (multiappsrc);
    return;
  }

  /* if info has pending ghostpad, it means that discont-push method was called.
   * in this case, we have to reconfigure pad-link with changing state of appsrc */
  if (info->pending_slot) {
    GstPad *pad;

    GST_DEBUG_OBJECT (info->appsrc, "pending slot detected on source_id (%s)",
        source_id);
    QUEUE_LOCK (info);
    gst_element_set_state (GST_ELEMENT_CAST (info->appsrc), GST_STATE_READY);
    /* unlink and remove current ghostpad */
    GST_DEBUG_OBJECT (info->appsrc, "remove current active_slot");

    free_output_slot (info->active_slot);

    /* switch active_slot */
    info->active_slot = info->pending_slot;

    /* link new ghostpad to appsrc */
    GST_DEBUG_OBJECT (info->appsrc, "link new ghostpad");
    pad = gst_element_get_static_pad (info->appsrc, "src");
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (info->active_slot->srcpad),
        pad);
    gst_object_unref (pad);

    gst_element_set_locked_state (info->appsrc, FALSE);
    gst_element_sync_state_with_parent (GST_ELEMENT_CAST (info->appsrc));
    gst_pad_remove_probe (info->active_slot->srcpad, info->block_id);
    info->block_id = 0;

    /* At this point, appsrc will post new startup seek_data_cb with ZERO position,
     * but it should be dropped since we are already running state. */

    /* we can drain all data after seek_data_cb is returned */
    drain_pending_data (info);
    info->do_reconfigure = FALSE;
    info->reconfigure_posted = FALSE;
    info->pending_slot = NULL;
    g_cond_broadcast (&info->cond);
    QUEUE_UNLOCK (info);

    GST_DEBUG_OBJECT (info->appsrc, "reconfigure finished");

  } else {
    GST_DEBUG_OBJECT (info->appsrc, "no pending pad on source_id (%s)",
        source_id);
    g_hash_table_remove (priv->appsrc_info_id_pairs, source_id);
  }
  HASH_UNLOCK (multiappsrc);
  gst_message_unref (msg);

  return;
}

static gchar *
gen_source_id (const gchar * source_id)
{
  gchar *generated_id;
  gchar *generated_source_id = NULL, *new_source_id;

  GChecksum *cs;
  cs = g_checksum_new (G_CHECKSUM_SHA256);

  generated_id =
      g_strdup_printf ("%08x%08x%08x%08x", g_random_int (), g_random_int (),
      g_random_int (), g_random_int ());

  g_checksum_update (cs, (const guchar *) generated_id, strlen (generated_id));
  generated_source_id = g_strdup (g_checksum_get_string (cs));

  g_free (generated_id);
  g_checksum_free (cs);

  if (source_id) {
    new_source_id = g_strconcat (generated_source_id, "/", source_id, NULL);
  } else {
    new_source_id = g_strdup (generated_source_id);
  }

  g_free (generated_source_id);

  return new_source_id;
}

gchar *
gst_multi_appsrc_add_source_id (GstMultiAppSrc * multiappsrc,
    const gchar * name)
{
  gchar *source_id;
  ChildAppSrcInfo *info;
  GstMultiAppSrcPrivate *priv;
  GstBin *bin;
  GstAppSrcCallbacks appsrc_callbacks = { 0 };

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), NULL);

  bin = GST_BIN_CAST (multiappsrc);

  MULTI_APPSRC_LOCK (multiappsrc);
  if (name != NULL &&
      G_UNLIKELY (!gst_object_check_uniqueness (bin->children, name))) {
    MULTI_APPSRC_UNLOCK (multiappsrc);
    GST_DEBUG_OBJECT (multiappsrc,
        "duplicated name already exist in multiappsrc");
    return NULL;
  }

  priv = multiappsrc->priv;

  source_id = gen_source_id (name);

  info = g_malloc0 (sizeof (ChildAppSrcInfo));
  info->source_id = g_strdup (source_id);
  info->multiappsrc = gst_object_ref (multiappsrc);
  info->appsrc = gst_element_factory_make ("appsrc", name);
  info->pending_data = g_queue_new ();

  g_mutex_init (&info->queue_lock);
  g_cond_init (&info->cond);

  HASH_LOCK (multiappsrc);
  g_hash_table_insert (priv->appsrc_info_id_pairs, g_strdup (source_id), info);
  HASH_UNLOCK (multiappsrc);

  GST_DEBUG_OBJECT (info->appsrc, "appsrc is created (id:%s) %p",
      info->source_id, info->appsrc);

  appsrc_callbacks.need_data = appsrc_need_data_cb;
  appsrc_callbacks.enough_data = appsrc_enough_data_cb;
  appsrc_callbacks.seek_data = appsrc_seek_data_cb;

  gst_app_src_set_callbacks (GST_APP_SRC (info->appsrc), &appsrc_callbacks,
      info, NULL);

  gst_multi_appsrc_set_stream_type_internal (multiappsrc, info);
  gst_multi_appsrc_set_format_internal (multiappsrc, info);

  gst_bin_add (bin, info->appsrc);

  g_signal_emit (multiappsrc, gst_multi_appsrc_signals[SIGNAL_SOURCE_ADDED], 0,
      source_id, name, NULL);

  MULTI_APPSRC_UNLOCK (multiappsrc);

  if (priv->smart_prop)
    apply_smart_properties (multiappsrc, info);

  if (priv->running) {
    QUEUE_LOCK (info);
    expose_slot (multiappsrc, info, FALSE);
    sync_internal_appsrc_state (source_id, info, NULL);
    QUEUE_UNLOCK (info);
  }

  return source_id;
}

static GstPadProbeReturn
srcpad_event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  ChildAppSrcInfo *child_info = CHILD_APPSRC_INFO_CAST (user_data);
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC (child_info->multiappsrc);
  GstMultiAppSrcPrivate *priv = multiappsrc->priv;
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_EOS:
      {
        GstMessage *msg;
        GstStructure *str;
        GST_DEBUG_OBJECT (pad, "Got event %s", GST_EVENT_TYPE_NAME (ev));
        QUEUE_LOCK (child_info);
        /* post message only do_reconfigure case */
        if (child_info->do_reconfigure && !child_info->reconfigure_posted) {
          str = gst_structure_new_empty ("source-drained");
          gst_structure_set (str, "source-id", G_TYPE_STRING,
              child_info->source_id, NULL);
          msg = gst_message_new_element (GST_OBJECT (multiappsrc), str);

          GST_DEBUG_OBJECT (pad, "posting source-drained message: %"
              GST_PTR_FORMAT, msg);
          gst_bus_post (priv->bus, msg);
          child_info->reconfigure_posted = TRUE;
        }
        QUEUE_UNLOCK (child_info);
        break;
      }
      case GST_EVENT_FLUSH_START:
        GST_DEBUG_OBJECT (pad, "Got event %s", GST_EVENT_TYPE_NAME (ev));
        g_cond_broadcast (&child_info->cond);
        break;
      case GST_EVENT_FLUSH_STOP:
      {
        GstMiniObject *obj;
        QUEUE_LOCK (child_info);
        /* flush queued data */
        while (!g_queue_is_empty (child_info->pending_data)) {
          obj = g_queue_pop_head (child_info->pending_data);
          if (obj)
            gst_mini_object_unref (obj);
        }

        QUEUE_UNLOCK (child_info);
      }
        break;
      default:
        break;
    }
  }

  return ret;
}

static GstPadProbeReturn
srcpad_block_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_PASS;
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_SEEK:
        GST_DEBUG_OBJECT (pad, "block seek to pending pad");
        ret = GST_PAD_PROBE_OK;
        break;
      default:
        break;
    }
  }

  return ret;
}

gboolean
gst_multi_appsrc_remove_source_id (GstMultiAppSrc * multiappsrc,
    gchar * source_id)
{
  GstMultiAppSrcPrivate *priv;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), FALSE);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);

  GST_LOG_OBJECT (multiappsrc, "removing source (id: %s)", source_id);

  if (priv->running) {
    ChildAppSrcInfo *info;
    guint n_source;

    /* unlock temporary to check the number of source */
    n_source = gst_multi_appsrc_get_num_source (multiappsrc);

    /* removing source on PAUSED or PLAYING state is restricted to multiple
     * appsrc info case */
    if (n_source < 2) {
      goto refuse;
    }

    HASH_LOCK (multiappsrc);
    info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
    HASH_UNLOCK (multiappsrc);

    if (G_UNLIKELY (!info))
      goto error;

    GST_LOG_OBJECT (multiappsrc, "trying to remove source (id: %s) on runtime",
        source_id);

    /* In running state, we have to drain all data in appsrc element.
     * Although event probe can catch draining status up by checking eos event,
     * the probe cannot clean the corresponding appsrc due to it's streaming thread.
     * So, the probe will post "source-drained" message to internal bus, and it
     * will stored in async queue. Finally, reconfigure task function will clean the appsrc info.
     *
     * what we should do in here is that,
     * - removing pending_slot (if exist) in order to notify "its remove_source_id case"
     * - set do_reconfigure
     */
    QUEUE_LOCK (info);

    if (info->pending_slot) {
      free_output_slot (info->pending_slot);
      info->pending_slot = NULL;
    }

    /* free object data to indicate this is removing pad */
    g_object_steal_data (G_OBJECT (info->active_slot->srcpad),
        "multiappsrc.outputslot");


    /* lock state of appsrc.
     * we don't want affect appsrc's state from parent's state change */
    gst_element_set_locked_state (info->appsrc, TRUE);

    info->do_reconfigure = TRUE;
    QUEUE_UNLOCK (info);

    gst_app_src_end_of_stream (GST_APP_SRC (info->appsrc));
  } else {
    gboolean ret;
    /* if we are not running state, appsrc info can be safely removed */
    HASH_LOCK (multiappsrc);
    ret = g_hash_table_remove (priv->appsrc_info_id_pairs, source_id);
    HASH_UNLOCK (multiappsrc);

    if (!ret)
      goto error;
  }

  MULTI_APPSRC_UNLOCK (multiappsrc);

  return TRUE;

refuse:
  {
    MULTI_APPSRC_UNLOCK (multiappsrc);
    GST_WARNING_OBJECT (multiappsrc,
        "refuse remove-source-id, it's allowed to only multiple appsrc exist");

    return FALSE;
  }

error:
  {
    MULTI_APPSRC_UNLOCK (multiappsrc);
    GST_WARNING_OBJECT (multiappsrc,
        "tried to remove non-existed id element(id: %s)", source_id);

    return FALSE;
  }
}

GstAppSrc *
gst_multi_appsrc_get_appsrc (GstMultiAppSrc * multiappsrc, gchar * source_id)
{
  GstAppSrc *appsrc;
  ChildAppSrcInfo *info;
  GstMultiAppSrcPrivate *priv;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), NULL);

  priv = multiappsrc->priv;

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  if (!info)
    goto error;
  appsrc = gst_object_ref (GST_APP_SRC (info->appsrc));
  HASH_UNLOCK (multiappsrc);

  return appsrc;

error:
  {
    MULTI_APPSRC_UNLOCK (multiappsrc);
    GST_ERROR_OBJECT (multiappsrc,
        "tried to get non-existed id element(id: %s)", source_id);

    return NULL;
  }
}

/* must be called with HASH_LOCK */
static GstFlowReturn
gst_multi_appsrc_end_of_stream_internal (GstMultiAppSrc * multiappsrc,
    ChildAppSrcInfo * info)
{
  GstFlowReturn ret = GST_FLOW_OK;;

  if (G_UNLIKELY (!info)) {
    GST_ERROR_OBJECT (multiappsrc, "null info detected");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (info->appsrc, "end-of-stream");

  QUEUE_LOCK (info);
  if (info->do_reconfigure) {
    GST_LOG_OBJECT (info->appsrc, "we are doing reconfigure");
    if (!info->pending_slot) {
      /* if we are in here, application sent eos to removing pad */
      GST_LOG_OBJECT (info->appsrc, "ignore end-of-stream on removing appsrc");
    } else {
      /* push eos to internal queue */
      GST_LOG_OBJECT (info->appsrc, "push end-of-stream in queue");
      g_queue_push_tail (info->pending_data, gst_event_new_eos ());
    }
    QUEUE_UNLOCK (info);

    return GST_FLOW_OK;
  }

  QUEUE_UNLOCK (info);

  ret = gst_app_src_end_of_stream (GST_APP_SRC (info->appsrc));

  return ret;
}

/**
 * gst_multi_appsrc_end_of_stream:
 * @multiappsrc: a #GstMultiAppSrc
 *
 * Indicates to all of appsrc elements that the last buffer queued in the
 * element is the last buffer of the stream.
 *
 * Returns: #GST_FLOW_OK when the EOS was successfuly queued.
 * #GST_FLOW_FLUSHING when @multiappsrc is not PAUSED or PLAYING.
 */

GstFlowReturn
gst_multi_appsrc_end_of_stream (GstMultiAppSrc * multiappsrc, gchar * source_id)
{
  GstMultiAppSrcPrivate *priv;
  GstFlowReturn ret = GST_FLOW_OK;
  GHashTableIter iter;
  gpointer key, value;
  ChildAppSrcInfo *info;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), GST_FLOW_ERROR);

  priv = multiappsrc->priv;

  if (!priv->running)
    goto flushing;

  /* if @source_id is not null, send end-of-stream to corresponding appsrc */
  if (source_id) {
    HASH_LOCK (multiappsrc);
    info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);

    ret = gst_multi_appsrc_end_of_stream_internal (multiappsrc, info);
    GST_DEBUG_OBJECT (info->appsrc, "sending EOS: ret = %s",
        gst_flow_get_name (ret));
    HASH_UNLOCK (multiappsrc);
    goto done;
  } else {
    GstFlowReturn flow_ret = GST_FLOW_OK;
    HASH_LOCK (multiappsrc);
    g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      info = CHILD_APPSRC_INFO_CAST (value);
      flow_ret = gst_multi_appsrc_end_of_stream_internal (multiappsrc, info);
      GST_DEBUG_OBJECT (multiappsrc, "sending EOS: ret = %s",
          gst_flow_get_name (ret));

      if (ret != GST_FLOW_OK) {
        GST_WARNING_OBJECT (info->appsrc,
            "giving up sending eos due to returning %s",
            gst_flow_get_name (ret));

        ret = flow_ret;
      }
    }
    HASH_UNLOCK (multiappsrc);
  }

done:
  return ret;

flushing:
  {
    GST_WARNING_OBJECT (multiappsrc,
        "EOS can be sent only in PLAYING or PAUSED state");
    return GST_FLOW_FLUSHING;
  }
}

gboolean
gst_multi_appsrc_send_event (GstMultiAppSrc * multiappsrc, gchar * source_id,
    GstEvent * event)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info;
  gboolean ret;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), FALSE);

  priv = multiappsrc->priv;

  GST_DEBUG_OBJECT (multiappsrc, "Got event %s on source id (%s)",
      GST_EVENT_TYPE_NAME (event), source_id);


  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (G_UNLIKELY (!info))
    goto error;

  QUEUE_LOCK (info);
  if (GST_EVENT_IS_SERIALIZED (event) ||
      GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START) {
    if (wait_reconfigure (info) == GST_FLOW_EOS) {
      QUEUE_UNLOCK (info);
      goto error;
    }
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_BOTH:
    case GST_EVENT_PROTECTION:
    {
      GST_LOG_OBJECT (info->appsrc, "append pending event");
      info->pending_events = g_list_append (info->pending_events, event);
      info->have_events = TRUE;
      ret = TRUE;
      QUEUE_UNLOCK (info);
      goto done;
    }
    default:
      break;
  }

  QUEUE_UNLOCK (info);

  ret = gst_element_send_event (info->appsrc, event);

done:
  return ret;

error:
  GST_ERROR_OBJECT (multiappsrc,
      "cannot find a proper internal appsrc by source_id (%s)", source_id);
  gst_event_unref (event);

  return FALSE;
}

static GstFlowReturn
gst_multi_appsrc_push_buffer_full (GstMultiAppSrc * multiappsrc, gchar *
    source_id, GstBuffer * buffer, gboolean steal_ref)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info;
  GstFlowReturn ret = GST_FLOW_OK;
  GstPad *srcpad;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  priv = multiappsrc->priv;

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (G_UNLIKELY (!info))
    goto error;

  srcpad = gst_element_get_static_pad (GST_APP_SRC (info->appsrc), "src");
  if (G_LIKELY (srcpad)) {
    if (G_UNLIKELY (GST_PAD_IS_FLUSHING (srcpad))) {
      gst_object_unref (srcpad);
      goto flushing;
    }

    gst_object_unref (srcpad);
  }

  if (!steal_ref)
    gst_buffer_ref (buffer);

  /* do not call push_buffer appsrc API when we have pending data.
   * To ensure serialized buffer sequence, push this buffer to the queue */
  QUEUE_LOCK (info);
  ret = wait_reconfigure (info);

  if (ret != GST_FLOW_OK) {
    QUEUE_UNLOCK (info);
    goto error;
  }

  drain_pending_events (info);

  QUEUE_UNLOCK (info);

  ret = gst_app_src_push_buffer (GST_APP_SRC (info->appsrc), buffer);

  return ret;

flushing:
  {
    GST_DEBUG_OBJECT (multiappsrc, "refuse buffer %p, we are flushing", buffer);
    if (steal_ref)
      gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

error:
  {
    GST_ERROR_OBJECT (multiappsrc,
        "cannot find a proper internal appsrc by source_id (%s)", source_id);
    if (steal_ref)
      gst_buffer_unref (buffer);

    return GST_FLOW_ERROR;
  }
}


static GstFlowReturn
gst_multi_appsrc_push_buffer_action (GstMultiAppSrc * multiappsrc, gchar *
    source_id, GstBuffer * buffer)
{
  return gst_multi_appsrc_push_buffer_full (multiappsrc, source_id, buffer,
      FALSE);
}

GstFlowReturn
gst_multi_appsrc_push_buffer (GstMultiAppSrc * multiappsrc, gchar * source_id,
    GstBuffer * buffer)
{
  return gst_multi_appsrc_push_buffer_full (multiappsrc, source_id, buffer,
      TRUE);
}

static GstFlowReturn
gst_multi_appsrc_push_discont_buffer_full (GstMultiAppSrc * multiappsrc, gchar *
    source_id, GstBuffer * buffer, gboolean steal_ref)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info;
  GstFlowReturn ret;
  GstPad *srcpad;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  priv = multiappsrc->priv;

  GST_LOG_OBJECT (multiappsrc, "Push discont buffer on source (id: %s)",
      source_id);

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (G_UNLIKELY (!info))
    goto error;

  srcpad = gst_element_get_static_pad (GST_APP_SRC (info->appsrc), "src");
  if (G_LIKELY (srcpad)) {
    if (G_UNLIKELY (GST_PAD_IS_FLUSHING (srcpad))) {
      gst_object_unref (srcpad);
      goto flushing;
    }

    gst_object_unref (srcpad);
  }

  if (!steal_ref)
    gst_buffer_ref (buffer);

  if (G_UNLIKELY (!priv->running)) {
    return gst_app_src_push_buffer (GST_APP_SRC (info->appsrc), buffer);
  }

  QUEUE_LOCK (info);
  ret = wait_reconfigure (info);

  if (ret != GST_FLOW_OK) {
    QUEUE_UNLOCK (info);
    goto error;
  }

  /* queueing current buffer which will be pushed after eos */
  info->do_reconfigure = TRUE;

  /* lock state of appsrc.
   * we don't want affect appsrc's state from parent's state change */
  gst_element_set_locked_state (info->appsrc, TRUE);

  g_queue_push_tail (info->pending_data, buffer);

  /* Make and expose ghost pad. This pad will be stored at info->pending_slot,
   * The pending_slot will be linked when we finish handling of discont buffer */
  expose_slot (multiappsrc, info, TRUE);

  MULTI_APPSRC_LOCK (multiappsrc);
  if (priv->do_seek) {
    GST_DEBUG_OBJECT (multiappsrc, "wait seeking done");
    QUEUE_UNLOCK (info);
    g_cond_wait (&priv->cond, &priv->lock);
    QUEUE_LOCK (info);
    GST_DEBUG_OBJECT (multiappsrc, "continue push_discont_buffer");
  }
  MULTI_APPSRC_UNLOCK (multiappsrc);

  ret = gst_app_src_end_of_stream (GST_APP_SRC (info->appsrc));
  QUEUE_UNLOCK (info);

  return ret;

flushing:
  {
    GST_DEBUG_OBJECT (multiappsrc, "refuse buffer %p, we are flushing", buffer);
    if (steal_ref)
      gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

error:
  {
    GST_ERROR_OBJECT (multiappsrc,
        "tried to push discont buffer on non-existed id element(id: %s)",
        source_id);
    if (steal_ref)
      gst_buffer_unref (buffer);

    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_multi_appsrc_push_discont_buffer_action (GstMultiAppSrc * multiappsrc, gchar
    * source_id, GstBuffer * buffer)
{
  return gst_multi_appsrc_push_discont_buffer_full (multiappsrc, source_id,
      buffer, FALSE);
}

GstFlowReturn
gst_multi_appsrc_push_discont_buffer (GstMultiAppSrc * multiappsrc, gchar *
    source_id, GstBuffer * buffer)
{
  return gst_multi_appsrc_push_discont_buffer_full (multiappsrc, source_id,
      buffer, TRUE);
}

GstFlowReturn
gst_multi_appsrc_push_sample (GstMultiAppSrc * multiappsrc, gchar * source_id,
    GstSample * sample)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info;
  GstFlowReturn ret = GST_FLOW_OK;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_SAMPLE (sample), GST_FLOW_ERROR);

  priv = multiappsrc->priv;

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (G_UNLIKELY (!info))
    goto error;

  QUEUE_LOCK (info);
  ret = wait_reconfigure (info);

  if (ret != GST_FLOW_OK) {
    QUEUE_UNLOCK (info);
    goto error;
  }

  drain_pending_events (info);

  QUEUE_UNLOCK (info);

  ret = gst_app_src_push_sample (GST_APP_SRC (info->appsrc), sample);

  return ret;

error:
  GST_ERROR_OBJECT (multiappsrc,
      "cannot find a proper internal appsrc by source_id (%s)", source_id);

  return GST_FLOW_ERROR;
}

GstFlowReturn
gst_multi_appsrc_push_discont_sample (GstMultiAppSrc * multiappsrc, gchar *
    source_id, GstSample * sample)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info;
  GstCaps *caps;
  GstBuffer *buffer;
  GstSegment *segment;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_SAMPLE (sample), GST_FLOW_ERROR);

  priv = multiappsrc->priv;

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (G_UNLIKELY (!info))
    goto error;

  if (G_UNLIKELY (!priv->running)) {
    return gst_app_src_push_sample (GST_APP_SRC (info->appsrc), sample);
  }

  QUEUE_LOCK (info);
  ret = wait_reconfigure (info);

  if (ret != GST_FLOW_OK) {
    QUEUE_UNLOCK (info);
    goto error;
  }

  /* queueing current buffer which will be pushed after eos */
  info->do_reconfigure = TRUE;

  /* lock state of appsrc.
   * we don't want affect appsrc's state from parent's state change */
  gst_element_set_locked_state (info->appsrc, TRUE);

  /* do not call push_sample appsrc API when we have pending data.
   * To ensure serialized buffer sequence, push this buffer to the queue */
  caps = gst_sample_get_caps (sample);
  buffer = gst_sample_get_buffer (sample);
  segment = gst_sample_get_segment (sample);

  /* Application might need send segment to AppSrc, but it's not supported now.
   * Please refer to https://bugzilla.gnome.org/show_bug.cgi?id=768510  */
  g_queue_push_tail (info->pending_data,
      gst_sample_new (buffer, caps, segment, NULL));

  /* Make and expose ghost pad. This pad will be stored at info->pending_slot,
   * The pending_slot will be linked when we finish handling of discont buffer */
  expose_slot (multiappsrc, info, TRUE);

  MULTI_APPSRC_LOCK (multiappsrc);
  if (priv->do_seek) {
    GST_DEBUG_OBJECT (multiappsrc, "wait seeking done");
    QUEUE_UNLOCK (info);
    g_cond_wait (&priv->cond, &priv->lock);
    QUEUE_LOCK (info);
    GST_DEBUG_OBJECT (multiappsrc, "continue push_discont_sample");
  }
  MULTI_APPSRC_UNLOCK (multiappsrc);

  ret = gst_app_src_end_of_stream (GST_APP_SRC (info->appsrc));
  QUEUE_UNLOCK (info);

  return ret;

error:
  GST_ERROR_OBJECT (multiappsrc,
      "cannot find a proper internal appsrc by source_id (%s)", source_id);

  return GST_FLOW_ERROR;
}


void
gst_multi_appsrc_set_emit_signals (GstMultiAppSrc * multiappsrc, gboolean emit)
{
  GstMultiAppSrcPrivate *priv;

  g_return_if_fail (GST_IS_MULTI_APPSRC (multiappsrc));

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  priv->emit_signals = emit;
  MULTI_APPSRC_UNLOCK (multiappsrc);
}

gboolean
gst_multi_appsrc_get_emit_signals (GstMultiAppSrc * multiappsrc)
{
  gboolean result;
  GstMultiAppSrcPrivate *priv;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), FALSE);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  result = priv->emit_signals;
  MULTI_APPSRC_UNLOCK (multiappsrc);

  return result;
}

/* must be called with MULTI_APPSRC_LOCK */
static void
gst_multi_appsrc_set_stream_type_internal (GstMultiAppSrc * multiappsrc,
    ChildAppSrcInfo * info)
{
  GstMultiAppSrcPrivate *priv;

  g_return_if_fail (info);
  g_return_if_fail (info->appsrc);

  priv = multiappsrc->priv;

  GST_DEBUG_OBJECT (info->appsrc, "Set stream-type to %d, source id (%s)",
      priv->stream_type, info->source_id);

  g_object_set (G_OBJECT (info->appsrc), "stream-type", priv->stream_type,
      NULL);
}

void
gst_multi_appsrc_set_stream_type (GstMultiAppSrc * multiappsrc,
    GstAppStreamType type)
{
  GstMultiAppSrcPrivate *priv;
  GHashTableIter iter;
  gpointer key, value;

  g_return_if_fail (GST_IS_MULTI_APPSRC (multiappsrc));

  priv = multiappsrc->priv;

  GST_DEBUG_OBJECT (multiappsrc, "Set stream-type to %d", type);

  MULTI_APPSRC_LOCK (multiappsrc);
  priv->stream_type = type;

  HASH_LOCK (multiappsrc);
  g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    gst_multi_appsrc_set_stream_type_internal (multiappsrc,
        CHILD_APPSRC_INFO_CAST (value));
  }
  HASH_UNLOCK (multiappsrc);
  MULTI_APPSRC_UNLOCK (multiappsrc);
}

GstAppStreamType
gst_multi_appsrc_get_stream_type (GstMultiAppSrc * multiappsrc)
{
  GstAppStreamType type;
  GstMultiAppSrcPrivate *priv;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc),
      DEFAULT_PROP_STREAM_TYPE);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  type = priv->stream_type;
  MULTI_APPSRC_UNLOCK (multiappsrc);

  return type;
}

/* must be called with MULTI_APPSRC_LOCK */
static void
gst_multi_appsrc_set_format_internal (GstMultiAppSrc * multiappsrc,
    ChildAppSrcInfo * info)
{
  GstMultiAppSrcPrivate *priv;

  g_return_if_fail (info);
  g_return_if_fail (info->appsrc);

  priv = multiappsrc->priv;

  GST_DEBUG_OBJECT (info->appsrc, "Set format to %s, source id (%s)",
      gst_format_get_name (priv->format), info->source_id);

  g_object_set (G_OBJECT (info->appsrc), "format", priv->format, NULL);
}

void
gst_multi_appsrc_set_format (GstMultiAppSrc * multiappsrc, GstFormat format)
{
  GstMultiAppSrcPrivate *priv;
  GHashTableIter iter;
  gpointer key, value;

  g_return_if_fail (GST_IS_MULTI_APPSRC (multiappsrc));

  priv = multiappsrc->priv;

  GST_DEBUG_OBJECT (multiappsrc, "Set format to %s",
      gst_format_get_name (format));

  MULTI_APPSRC_LOCK (multiappsrc);
  priv->format = format;

  HASH_LOCK (multiappsrc);
  g_hash_table_iter_init (&iter, priv->appsrc_info_id_pairs);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    gst_multi_appsrc_set_format_internal (multiappsrc,
        CHILD_APPSRC_INFO_CAST (value));
  }
  HASH_UNLOCK (multiappsrc);
  MULTI_APPSRC_UNLOCK (multiappsrc);
}

GstFormat
gst_multi_appsrc_get_format (GstMultiAppSrc * multiappsrc)
{
  GstFormat format;
  GstMultiAppSrcPrivate *priv;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc),
      GST_FORMAT_UNDEFINED);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  format = priv->format;
  MULTI_APPSRC_UNLOCK (multiappsrc);

  return format;
}

void
gst_multi_appsrc_set_caps (GstMultiAppSrc * multiappsrc, gchar * source_id,
    const GstCaps * caps)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info = NULL;

  g_return_if_fail (GST_IS_MULTI_APPSRC (multiappsrc));

  priv = multiappsrc->priv;

  GST_DEBUG_OBJECT (multiappsrc, "Trying to set caps on source id (%s)",
      source_id);

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (info) {
    gst_app_src_set_caps (GST_APP_SRC (info->appsrc), caps);
    GST_DEBUG_OBJECT (info->appsrc, "Set caps done on source id (%s)",
        source_id);
  } else {
    GST_ERROR_OBJECT (multiappsrc, "Invalid source id (%s)", source_id);
  }
}

GstCaps *
gst_multi_appsrc_get_caps (GstMultiAppSrc * multiappsrc, gchar * source_id)
{
  GstMultiAppSrcPrivate *priv;
  ChildAppSrcInfo *info = NULL;
  GstCaps *caps = NULL;

  g_return_val_if_fail (GST_IS_MULTI_APPSRC (multiappsrc), NULL);

  priv = multiappsrc->priv;

  HASH_LOCK (multiappsrc);
  info = g_hash_table_lookup (priv->appsrc_info_id_pairs, source_id);
  HASH_UNLOCK (multiappsrc);

  if (info) {
    caps = gst_app_src_get_caps (GST_APP_SRC (info->appsrc));
  }

  return caps;
}

static void
appsrc_need_data_cb (GstAppSrc * src, guint length, gpointer user_data)
{
  GstMultiAppSrc *multiappsrc;
  GstMultiAppSrcPrivate *priv;
  gboolean emit;

  ChildAppSrcInfo *info = CHILD_APPSRC_INFO_CAST (user_data);
  g_assert (info);

  multiappsrc = GST_MULTI_APPSRC (info->multiappsrc);
  priv = multiappsrc->priv;

  /* FIXME: maybe need lock here */
  if (info->do_reconfigure) {
    /* if we are doing reconfigure, ignore need-data */
    GST_LOG_OBJECT (info->appsrc,
        "drop need-data callback on doing reconfigure");
    return;
  }

  MULTI_APPSRC_LOCK (multiappsrc);
  emit = priv->emit_signals;
  MULTI_APPSRC_UNLOCK (multiappsrc);

  GST_TRACE_OBJECT (info->appsrc, "need-data");

  if (priv->callbacks.need_data) {
    priv->callbacks.need_data (multiappsrc, info->source_id, length,
        priv->user_data);
  } else if (emit) {
    g_signal_emit (multiappsrc, gst_multi_appsrc_signals[SIGNAL_NEED_DATA], 0,
        info->source_id, length, NULL);
  }
}

static void
appsrc_enough_data_cb (GstAppSrc * src, gpointer user_data)
{
  GstMultiAppSrc *multiappsrc;
  GstMultiAppSrcPrivate *priv;
  gboolean emit;

  ChildAppSrcInfo *info = CHILD_APPSRC_INFO_CAST (user_data);
  g_assert (info);

  multiappsrc = GST_MULTI_APPSRC (info->multiappsrc);
  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  emit = priv->emit_signals;
  MULTI_APPSRC_UNLOCK (multiappsrc);

  GST_TRACE_OBJECT (info->appsrc, "seek-data");

  if (priv->callbacks.enough_data) {
    priv->callbacks.enough_data (multiappsrc, info->source_id, priv->user_data);
  } else if (emit) {
    g_signal_emit (multiappsrc, gst_multi_appsrc_signals[SIGNAL_ENOUGH_DATA], 0,
        info->source_id, NULL);
  }
}

static gboolean
appsrc_seek_data_cb (GstAppSrc * src, guint64 offset, gpointer user_data)
{
  GstMultiAppSrc *multiappsrc;
  GstMultiAppSrcPrivate *priv;
  gboolean res = TRUE;
  gboolean emit;

  ChildAppSrcInfo *info = CHILD_APPSRC_INFO_CAST (user_data);
  g_assert (info);

  multiappsrc = GST_MULTI_APPSRC (info->multiappsrc);
  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  if (!priv->do_seek && info->do_reconfigure) {
    MULTI_APPSRC_UNLOCK (multiappsrc);
    GST_LOG_OBJECT (info->appsrc,
        "drop seek-data callback on doing reconfigure");
    return TRUE;
  }

  emit = priv->emit_signals;

  MULTI_APPSRC_UNLOCK (multiappsrc);

  if (priv->callbacks.seek_data)
    res = priv->callbacks.seek_data (multiappsrc, info->source_id, offset,
        priv->user_data);
  else if (emit) {
    g_signal_emit (multiappsrc, gst_multi_appsrc_signals[SIGNAL_SEEK_DATA], 0,
        info->source_id, offset, &res);
  }

  return res;
}

/**
 * gst_multi_appsrc_set_callbacks:
 * @multiappsrc: a #GstMultiAppSrc
 * @callbacks: the callbacks
 * @user_data: a user_data argument for the callbacks
 * @notify: a destroy notify function
 *
 * Set callbacks which will be executed when data is needed, enough data has
 * been collected or when a seek should be performed.
 * This is an alternative to using the signals, it has lower overhead and is thus
 * less expensive, but also less flexible.
 *
 * If callbacks are installed, no signals will be emitted for performance
 * reasons.
 */
void
gst_multi_appsrc_set_callbacks (GstMultiAppSrc * multiappsrc,
    GstMultiAppSrcCallbacks * callbacks, gpointer user_data,
    GDestroyNotify notify)
{
  GDestroyNotify old_notify;
  GstMultiAppSrcPrivate *priv;

  g_return_if_fail (GST_IS_MULTI_APPSRC (multiappsrc));
  g_return_if_fail (callbacks != NULL);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  old_notify = priv->notify;

  if (old_notify) {
    gpointer old_data;

    old_data = priv->user_data;

    priv->user_data = NULL;
    priv->notify = NULL;
    MULTI_APPSRC_UNLOCK (multiappsrc);

    old_notify (old_data);

    MULTI_APPSRC_LOCK (multiappsrc);
  }
  priv->callbacks = *callbacks;
  priv->user_data = user_data;
  priv->notify = notify;
  MULTI_APPSRC_UNLOCK (multiappsrc);
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_multi_appsrc_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_multi_appsrc_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "multiappsrc", NULL };

  return protocols;
}

static gchar *
gst_multi_appsrc_uri_get_uri (GstURIHandler * handler)
{
  gchar *uri;
  GstMultiAppSrcPrivate *priv;
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC (handler);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  uri = g_strdup (priv->uri);
  MULTI_APPSRC_UNLOCK (multiappsrc);

  return uri;
}

static gboolean
gst_multi_appsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstMultiAppSrcPrivate *priv;
  GstMultiAppSrc *multiappsrc = GST_MULTI_APPSRC (handler);

  priv = multiappsrc->priv;

  MULTI_APPSRC_LOCK (multiappsrc);
  g_free (priv->uri);
  priv->uri = uri ? g_strdup (uri) : NULL;
  MULTI_APPSRC_UNLOCK (multiappsrc);

  return TRUE;
}

static void
gst_multi_appsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_multi_appsrc_uri_get_type;
  iface->get_protocols = gst_multi_appsrc_uri_get_protocols;
  iface->get_uri = gst_multi_appsrc_uri_get_uri;
  iface->set_uri = gst_multi_appsrc_uri_set_uri;
}
