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

#include <gst/gst.h>
#include <gst/base/base.h>

#ifndef GST_H265_COMBINER_H
#define GST_H265_COMBINER_H

#define GST_TYPE_H265_COMBINER            (gst_h265_combiner_get_type ())
#define GST_H265_COMBINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_H265_COMBINER, GstH265Combiner))
#define GST_H265_COMBINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_H265_COMBINER, GstH265CombinerClass))
#define GST_H265_COMBINER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_H265_COMBINER, GstH265CombinerClass))

typedef struct _GstH265Combiner GstH265Combiner;
typedef struct _GstH265CombinerClass GstH265CombinerClass;

struct _GstH265Combiner
{
  GstAggregator aggregator;

  /*< private >*/
  GstPad *base_sink;
  GstPad *enhancement_sink;
};

struct _GstH265CombinerClass
{
  GstAggregatorClass aggregator_class;
};

GType     gst_h265_combiner_get_type (void);

gboolean  gst_h265_combiner_plugin_init (GstPlugin * plugin);

#endif /* GST_H265_COMBINER_H */

