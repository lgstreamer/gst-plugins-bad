/* GStreamer H265 layer combiner
 * Copyright (c) 2018 Mathieu Duponchelle <mathieu@centricular.com>
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
 * SECTION:element-h265combiner
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth265combiner.h"

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, stream-format=byte-stream"));

static GstStaticPadTemplate base_sink_template =
GST_STATIC_PAD_TEMPLATE ("base_sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, stream-format=byte-stream"));

static GstStaticPadTemplate enhancement_sink_template =
GST_STATIC_PAD_TEMPLATE ("enhancement_sink", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-h265, stream-format=byte-stream"));

GST_DEBUG_CATEGORY_STATIC (gst_h265_combiner_debug);
#define GST_CAT_DEFAULT gst_h265_combiner_debug

G_DEFINE_TYPE (GstH265Combiner, gst_h265_combiner, GST_TYPE_AGGREGATOR);

static GstAggregatorPad *
gst_h265_combiner_create_new_pad (GstAggregator * aggregator,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstH265Combiner *combiner = GST_H265_COMBINER (aggregator);
  const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);

  if (g_strcmp0 (templ_name, "enhancement_sink") != 0) {
    GST_ERROR_OBJECT (combiner, "Unexpected pad template %s", templ_name);
    return NULL;
  }

  GST_OBJECT_LOCK (combiner);

  if (combiner->enhancement_sink != NULL) {
    GST_ERROR_OBJECT (combiner, "Pad for template %s already exists, can only "
        "have one", templ_name);
    GST_OBJECT_UNLOCK (combiner);
    return NULL;
  }

  combiner->enhancement_sink = (GstPad *) g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", templ_name, "direction", GST_PAD_SINK, "template", templ, NULL);

  GST_OBJECT_UNLOCK (combiner);

  return GST_AGGREGATOR_PAD_CAST (combiner->enhancement_sink);
}

static GstFlowReturn
gst_h265_combiner_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstH265Combiner *combiner = GST_H265_COMBINER (aggregator);
  GstAggregatorPad *base_sink = GST_AGGREGATOR_PAD (combiner->base_sink);
  GstAggregatorPad *enhancement_sink = NULL;
  GstBuffer *buf;
  GstFlowReturn ret;

  if (combiner->enhancement_sink != NULL)
    enhancement_sink = GST_AGGREGATOR_PAD (combiner->enhancement_sink);

  if (gst_aggregator_pad_is_eos (base_sink)) {
    if (enhancement_sink != NULL
        && !gst_aggregator_pad_is_eos (enhancement_sink)) {
      GST_WARNING_OBJECT (combiner,
          "Have more correction data, but main "
          "stream is already EOS, very unexpected!");
      gst_aggregator_pad_drop_buffer (enhancement_sink);
    }
    return GST_FLOW_EOS;
  }

  buf = gst_aggregator_pad_pop_buffer (base_sink);

  ret = gst_aggregator_finish_buffer (aggregator, buf);

  if (enhancement_sink) {
    buf = gst_aggregator_pad_pop_buffer (enhancement_sink);
    ret = gst_aggregator_finish_buffer (aggregator, buf);
  }

  return ret;
}

static void
gst_h265_combiner_class_init (GstH265CombinerClass * klass)
{
  GstAggregatorClass *aggregator_class = (GstAggregatorClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_add_static_pad_template (element_class,
      &base_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &enhancement_sink_template);

  gst_element_class_set_static_metadata (element_class, "H265 Combiner",
      "Codec/Combiner/Video", "H265 Layer Combiner",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  aggregator_class->aggregate = gst_h265_combiner_aggregate;
  aggregator_class->create_new_pad = gst_h265_combiner_create_new_pad;
}

static void
gst_h265_combiner_init (GstH265Combiner * combiner)
{
  GstAggregator *aggregator = GST_AGGREGATOR_CAST (combiner);

  combiner->base_sink = (GstPad *) g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", "base_sink", "direction", GST_PAD_SINK,
      "template", gst_static_pad_template_get (&base_sink_template), NULL);

  gst_element_add_pad (GST_ELEMENT (combiner), combiner->base_sink);

  gst_segment_init (&GST_AGGREGATOR_PAD (aggregator->srcpad)->segment,
      GST_FORMAT_TIME);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_h265_combiner_debug, "h265combiner", 0,
      "Wavpack correction data combiner");

  return gst_element_register (plugin, "h265combiner", GST_RANK_SECONDARY,
      GST_TYPE_H265_COMBINER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    h265combiner,
    "H265 combiner",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
