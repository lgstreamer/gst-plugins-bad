/* GStreamer Multiple App Sources Plugins
 *
 * Copyright (C) 2014 LG Electronics, Inc.
 *	Author : Wonchul Lee <wonchul86.lee@lge.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>

#include <gst/multiapp/gstmultiappsrc.h>

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "multiappsrc", GST_RANK_PRIMARY,
          GST_TYPE_MULTI_APPSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    multiappsrc,
    "Multiple App Source",
    plugin_init, PACKAGE_VERSION, "LGPL", PACKAGE_NAME, PACKAGE_BUGREPORT)
