/* GStreamer Multiple App Sources element
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

#ifndef __GST_MULTI_APPSRC_H__
#define __GST_MULTI_APPSRC_H__

#include <gst/gst.h>
#include <gst/app/app.h>

G_BEGIN_DECLS

#define GST_TYPE_MULTI_APPSRC (gst_multi_appsrc_get_type())
#define GST_MULTI_APPSRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTI_APPSRC,GstMultiAppSrc))
#define GST_MULTI_APPSRC_CLASS(obj) (G_TYPE_CHECK_CLASS_CAST((obj),GST_TYPE_MULTI_APPSRC,GstMultiAppSrcClass))
#define GST_IS_MULTI_APPSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTI_APPSRC))
#define GST_IS_MULTI_APPSRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((obj),GST_TYPE_MULTI_APPSRC))
#define GST_MULTI_APPSRC_CAST(obj) ((GstMultiAppSrc*)obj)

typedef struct _GstMultiAppSrc        GstMultiAppSrc;
typedef struct _GstMultiAppSrcClass   GstMultiAppSrcClass;
typedef struct _GstMultiAppSrcPrivate GstMultiAppSrcPrivate;

typedef struct {
  void      (*need_data)    (GstMultiAppSrc *multiappsrc, gchar *source_id, guint length, gpointer user_data);
  void      (*enough_data)  (GstMultiAppSrc *multiappsrc, gchar *source_id, gpointer user_data);
  gboolean  (*seek_data)    (GstMultiAppSrc *multiappsrc, gchar *source_id, guint64 offset, gpointer user_data);

  /*< private >*/
  gpointer     _gst_reserved[GST_PADDING];
} GstMultiAppSrcCallbacks;

/**
 * GstMultiAppSrc:
 *
 * multiappsrc element data structure
 */
struct _GstMultiAppSrc
{
  GstBin parent;

  /*< private >*/
  GstMultiAppSrcPrivate *priv;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMultiAppSrcClass
{
  GstBinClass parent_class;

  /* create a appsrc element */
  gchar *        (*add_source_id)      (GstMultiAppSrc *multiappsrc, const gchar *name);
  gboolean       (*remove_source_id)   (GstMultiAppSrc *multiappsrc, gchar *source_id);
  GstAppSrc *    (*get_appsrc)         (GstMultiAppSrc *multiappsrc, gchar *source_id);

  /* signals */
  void           (*need_data)          (GstMultiAppSrc *multiappsrc, gchar *source_id, guint length);
  void           (*enough_data)        (GstMultiAppSrc *multiappsrc, gchar *source_id);
  gboolean       (*seek_data)          (GstMultiAppSrc *multiappsrc, gchar *source_id, guint64 offset);
  void           (*source_added)       (GstMultiAppSrc *multiappsrc, gchar *source_id, const gchar *name);

  /* actions */
  GstFlowReturn  (*push_buffer)        (GstMultiAppSrc *multiappsrc, gchar *source_id, GstBuffer *buffer);
  GstFlowReturn  (*push_sample)        (GstMultiAppSrc *multiappsrc, gchar *source_id, GstSample *sample);
  GstFlowReturn  (*end_of_stream)      (GstMultiAppSrc *multiappsrc, gchar *source_id);

  GstFlowReturn  (*push_discont_buffer)  (GstMultiAppSrc *multiappsrc, gchar *source_id, GstBuffer *buffer);
  GstFlowReturn  (*push_discont_sample)  (GstMultiAppSrc *multiappsrc, gchar *source_id, GstSample *sample);

  gpointer _gst_reserved[GST_PADDING-1];
};

GST_EXPORT
GType gst_multi_appsrc_get_type (void);

GST_EXPORT
void             gst_multi_appsrc_set_emit_signals        (GstMultiAppSrc *multiappsrc, gboolean emit);

GST_EXPORT
gboolean         gst_multi_appsrc_get_emit_signals        (GstMultiAppSrc *multiappsrc);

GST_EXPORT
void             gst_multi_appsrc_set_stream_type         (GstMultiAppSrc *multiappsrc, GstAppStreamType type);

GST_EXPORT
GstAppStreamType gst_multi_appsrc_get_stream_type         (GstMultiAppSrc *multiappsrc);

GST_EXPORT
void             gst_multi_appsrc_set_format              (GstMultiAppSrc *multiappsrc, GstFormat format);

GST_EXPORT
GstFormat        gst_multi_appsrc_get_format              (GstMultiAppSrc *multiappsrc);

GST_EXPORT
void             gst_multi_appsrc_set_caps                (GstMultiAppSrc *multiappsrc, gchar *source_id, const GstCaps *caps);

GST_EXPORT
GstCaps*         gst_multi_appsrc_get_caps                (GstMultiAppSrc *multiappsrc, gchar *source_id);

GST_EXPORT
gchar *          gst_multi_appsrc_add_source_id           (GstMultiAppSrc *multiappsrc, const gchar *name);

GST_EXPORT
gboolean         gst_multi_appsrc_remove_source_id        (GstMultiAppSrc *multiappsrc, gchar *source_id);

GST_EXPORT
GstAppSrc *      gst_multi_appsrc_get_appsrc              (GstMultiAppSrc *multiappsrc, gchar *source_id);

GST_EXPORT
GstFlowReturn    gst_multi_appsrc_end_of_stream           (GstMultiAppSrc *multiappsrc, gchar *source_id);

GST_EXPORT
GstFlowReturn    gst_multi_appsrc_push_buffer             (GstMultiAppSrc *multiappsrc, gchar *source_id, GstBuffer *buffer);

GST_EXPORT
GstFlowReturn    gst_multi_appsrc_push_sample             (GstMultiAppSrc *multiappsrc, gchar *source_id, GstSample *sample);

GST_EXPORT
gboolean         gst_multi_appsrc_send_event              (GstMultiAppSrc *multiappsrc, gchar *source_id, GstEvent *event);

GST_EXPORT
GstFlowReturn    gst_multi_appsrc_push_discont_buffer     (GstMultiAppSrc *multiappsrc, gchar *source_id, GstBuffer *buffer);

GST_EXPORT
GstFlowReturn    gst_multi_appsrc_push_discont_sample     (GstMultiAppSrc *multiappsrc, gchar *source_id, GstSample *sample);

GST_EXPORT
void             gst_multi_appsrc_set_callbacks           (GstMultiAppSrc * multiappsrc,
                                                           GstMultiAppSrcCallbacks *callbacks,
                                                           gpointer user_data,
                                                           GDestroyNotify notify);

G_END_DECLS
#endif /* __GST_MULTI_APPSRC_H__ */
