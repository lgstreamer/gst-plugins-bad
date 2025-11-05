/*
* This file is part of GstBaseDrm
*
* Copyright (C) 2015-2016, STMicroelectronics - All Rights Reserved
* Authors:
* Jean-Christophe Trotin <jean-christophe.trotin@st.com>
* Rajesh Sharma <rajesh-dcg.sharma@st.com>
* for STMicroelectronics
*
* License terms: LGPL V2.1.
*
* GstBaseDrm is free software; you can redistribute it and/or modify it
* under the terms of the GNU Lesser General Public License version 2.1 as
* published by the Free Software Foundation.
*
* GstBaseDrm is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
* for more details.
*
* You should have received a copy of the GNU Lesser General Public License along
* with this library. If not, see <http://www.gnu.org/licenses/>.
*
*/

/**
 * SECTION:gstbasedrm
 * @short_description: Base class for DRM using ISOBMFF CENC common encryption and 'piff' (PSSH box)
 * Common encryption (CENC) in ISO base media file format files (ISOBMFF)  ISO/IEC 23001-7 standard
 * Common encryption (CENC) of MPEG-2 transport streams (MPEG-2 TS)        ISO/IEC 23001-9 standard
 *
 * @see_also:
 * This base class does license management and other common activities to enable DRM management systems
 * to decrypt the encrypted audio / video frames.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *   Initially gstbasedrm calls @start method of derived class when base transform
 *   calls its @start method.
 *   </para></listitem>
 *   <listitem><para>
 *   PSSH box is parsed in sink event handler and license acquisition methods are also called.
 *   if playback is allowed then @prepare_decrypt is called so that DRM system can prepare its
 *   decrypt context.
 *   </para></listitem>
 *   <listitem><para>
 *   @set_iv is called once a new Initialization Vector (IV) is received and @decrypt is
 *   called for each encrypted sample
 *   </para></listitem>
 *   </itemizedlist>
 *   </listitem>
 * </orderedlist>
 *
 * The subclass is responsible for providing pad template caps for
 * source and sink pads. The pads need to be named "sink" and "src".
 * DRM subsystem should do in place decryption so that input and output buffer are same.
 * gsbasedrm class will not implement any default methods therefore all necessary methods
 * required for DRM management should be implemented by derived class.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <libxml/parser.h>
#include <libsoup/soup.h>
#include <string.h>
#include "gstbasedrm.h"

#define GST_CAT_DEFAULT gst_basedrm_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* prototypes */
static void gst_basedrm_class_init (GstBaseDrmClass * klass);
static void gst_basedrm_init (GstBaseDrm * basedrm);
static gboolean gst_basedrm_sink_event_handler (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_basedrm_start (GstBaseTransform * trans);
static gboolean gst_basedrm_stop (GstBaseTransform * trans);
static GstCaps *gst_basedrm_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstFlowReturn gst_basedrm_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_basedrm_parse_protection_header (GstBaseDrm * drm,
    GstBuffer * pssi);
static gboolean gst_basedrm_setup_license (GstBaseDrm * basedrm,
    guint8 * header, guint size);
static void gst_basedrm_free_xml_data (GstContentProtection *
    content_protection_data, GstDRMSystemInfo * drm_system_info);
static gboolean gst_basedrm_get_xml_node_content (xmlNode * a_node,
    gchar ** content);
static gboolean gst_basedrm_get_xml_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);
static gboolean gst_basedrm_parse_content_protection_element (GstBaseDrm *
    basedrm, GstBuffer * pssi);
static void gst_basedrm_notify_rights_error_info (GstBaseDrm * basedrm);

#define UUID_STRING_LEN 36
#define KEY_ID_SIZE 16

static GMutex basedrm_mutex;
guint8 basedrm_mutex_init = 0;

/* class initialization */
static GstElementClass *parent_class = NULL;

/* Default implementations of subclass methods
 * all methods return FALSE to make basedrm implementation simple */
static gboolean
gst_basedrm_get_drm_info_default (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info)
{
  return FALSE;
}

static gboolean
gst_basedrm_start_default (GstBaseDrm * basedrm)
{
  return FALSE;
}

static gboolean
gst_basedrm_drm_init_default (GstBaseDrm * basedrm, guint8 * header, guint size)
{
  return FALSE;
}

static gboolean
gst_basedrm_get_license_challenge_default (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  return FALSE;
}

static gboolean
gst_basedrm_request_license_default (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  return FALSE;
}

static gboolean
gst_basedrm_store_license_default (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  return FALSE;
}

static gboolean
gst_basedrm_is_playback_allowed_default (GstBaseDrm * basedrm)
{
  return FALSE;
}

static gboolean
gst_basedrm_prepare_decrypt_default (GstBaseDrm * basedrm)
{
  return FALSE;
}

static gboolean
gst_basedrm_set_kid_default (GstBaseDrm * basedrm, guint8 * kid_data,
    gsize kid_size)
{
  return FALSE;
}

static gboolean
gst_basedrm_set_iv_default (GstBaseDrm * basedrm, guint8 * iv_data,
    gsize iv_size)
{
  return FALSE;
}

static gboolean
gst_basedrm_decrypt_default (GstBaseDrm * basedrm,
    GstDecryptInfo * decrypt_info)
{
  return FALSE;
}

static gboolean
gst_basedrm_resolve_custom_pssi_default (GstBaseDrm * basedrm,
    gchar * custom_pssi, guint8 ** header, guint * size, gboolean * ignore)
{
  return TRUE;
}

static GstBuffer *
gst_basedrm_get_key_info_default (GstBaseDrm * basedrm)
{
  return NULL;
}

static gboolean
gst_basedrm_stop_default (GstBaseDrm * basedrm)
{
  return FALSE;
}

static gboolean
gst_basedrm_restore_original_pssh_default (GstBaseDrm * basedrm,
    guint8 * original_pssh, guint original_pssh_size, guint8 ** data,
    guint * data_size)
{
  return TRUE;
}

static gboolean
gst_basedrm_set_video_info_default (GstBaseDrm * basedrm, guint32 width,
    guint32 height)
{
  return TRUE;
}

GType
gst_basedrm_get_type (void)
{
  static GType basedrm_type = 0;

  if (g_once_init_enter (&basedrm_type)) {
    GType _type;
    static const GTypeInfo basedrm_info = {
      sizeof (GstBaseDrmClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_basedrm_class_init,
      NULL,
      NULL,
      sizeof (GstBaseDrm),
      0,
      (GInstanceInitFunc) gst_basedrm_init,
    };

    _type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstBaseDrm", &basedrm_info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&basedrm_type, _type);
  }
  return basedrm_type;
}

static void
gst_basedrm_class_init (GstBaseDrmClass * klass)
{
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_basedrm_debug_category, "basedrm", 0,
      "Base DRM class for CENC (DASH) and PIFF (MSS) support");
  GST_DEBUG ("basedrm class init");

  parent_class = g_type_class_peek_parent (klass);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_basedrm_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_basedrm_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_basedrm_transform_ip);
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_basedrm_transform_caps);
  base_transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_basedrm_sink_event_handler);
  base_transform_class->transform_ip_on_passthrough = FALSE;

  /* Default implementations in basedrm class */
  klass->get_drm_info = GST_DEBUG_FUNCPTR (gst_basedrm_get_drm_info_default);
  klass->start = GST_DEBUG_FUNCPTR (gst_basedrm_start_default);
  klass->drm_init = GST_DEBUG_FUNCPTR (gst_basedrm_drm_init_default);
  klass->get_license_challenge =
      GST_DEBUG_FUNCPTR (gst_basedrm_get_license_challenge_default);
  klass->request_license =
      GST_DEBUG_FUNCPTR (gst_basedrm_request_license_default);
  klass->store_license = GST_DEBUG_FUNCPTR (gst_basedrm_store_license_default);
  klass->is_playback_allowed =
      GST_DEBUG_FUNCPTR (gst_basedrm_is_playback_allowed_default);
  klass->prepare_decrypt =
      GST_DEBUG_FUNCPTR (gst_basedrm_prepare_decrypt_default);
  klass->set_kid = GST_DEBUG_FUNCPTR (gst_basedrm_set_kid_default);
  klass->set_iv = GST_DEBUG_FUNCPTR (gst_basedrm_set_iv_default);
  klass->decrypt = GST_DEBUG_FUNCPTR (gst_basedrm_decrypt_default);
  klass->resolve_custom_pssi =
      GST_DEBUG_FUNCPTR (gst_basedrm_resolve_custom_pssi_default);
  klass->get_key_info = GST_DEBUG_FUNCPTR (gst_basedrm_get_key_info_default);
  klass->stop = GST_DEBUG_FUNCPTR (gst_basedrm_stop_default);
  klass->restore_original_pssh =
      GST_DEBUG_FUNCPTR (gst_basedrm_restore_original_pssh_default);
  klass->set_video_info =
      GST_DEBUG_FUNCPTR (gst_basedrm_set_video_info_default);

  if (basedrm_mutex_init == 0) {
    g_mutex_init (&basedrm_mutex);
    basedrm_mutex_init = 1;
  }
}

static void
sink_pad_linked_cb (GstPad * pad, GstPad * peer, GstBaseDrm * basedrm)
{
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;

  GST_DEBUG_OBJECT (basedrm, "sinkpad linked");

  g_free (drm_system_info->drmclient_id);
  gst_element_get_smart_properties (GST_ELEMENT_CAST (basedrm),
      "drm-clientid", &drm_system_info->drmclient_id, NULL);

  if (drm_system_info->drmclient_id) {
    GST_INFO_OBJECT (basedrm, "drm-clientid: %s",
        drm_system_info->drmclient_id);
  }
}

static void
gst_basedrm_init (GstBaseDrm * basedrm)
{
  GstDecryptInfo *decrypt_info = &basedrm->decrypt_info;
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDRMLicenseInfo *drm_license_info = &basedrm->drm_license_info;
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;

  GST_DEBUG_OBJECT (basedrm, "Base DRM init");
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (basedrm), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (basedrm), FALSE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (basedrm), FALSE);
  basedrm->proxy_id = NULL;
  basedrm->proxy_pw = NULL;
  decrypt_info->data = NULL;
  drm_system_info->system_ids = NULL;
  drm_system_info->soap_action = NULL;
  drm_system_info->xml_node_name = NULL;
  drm_system_info->drmclient_id = NULL;
  drm_system_info->is_svp = FALSE;
  drm_system_info->license_type = NULL;
  drm_system_info->content_id = NULL;
  drm_system_info->license_url = NULL;
  drm_system_info->group_license_url = NULL;
  drm_license_info->ack = NULL;
  drm_license_info->ack_response = NULL;
  drm_license_info->challenge = NULL;
  drm_license_info->response = NULL;
  drm_license_info->url = NULL;
  rights_error_info->error_state = RIGHTS_ERROR_NONE;
  rights_error_info->content_id = g_strdup ("unknown");
  rights_error_info->drm_system_id = g_strdup ("unknown");
  rights_error_info->rights_issuer_url = g_strdup ("unknown");

  g_signal_connect (G_OBJECT (GST_BASE_TRANSFORM (basedrm)->sinkpad), "linked",
      G_CALLBACK (sink_pad_linked_cb), basedrm);
}

static gboolean
gst_basedrm_start (GstBaseTransform * trans)
{
  GstBaseDrm *basedrm = GST_BASEDRM (trans);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);

  GST_DEBUG_OBJECT (basedrm, "start");

  /* start the DRM system */
  g_mutex_lock (&basedrm_mutex);
  if (basedrm_class->start (basedrm) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to start DRM session");
    g_mutex_unlock (&basedrm_mutex);
    return FALSE;
  }
  g_mutex_unlock (&basedrm_mutex);
  return TRUE;
}

static gboolean
gst_basedrm_stop (GstBaseTransform * trans)
{
  GstBaseDrm *basedrm = GST_BASEDRM (trans);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;

  GST_DEBUG_OBJECT (basedrm, "stop");

  /* stop the DRM system */
  g_mutex_lock (&basedrm_mutex);
  if (basedrm_class->stop (basedrm) == FALSE)
    GST_WARNING_OBJECT (basedrm, "Failed to stop the DRM system");
  g_mutex_unlock (&basedrm_mutex);

  /* release the proxy name and password */
  if (basedrm->proxy_id) {
    g_free ((gpointer) basedrm->proxy_id);
    basedrm->proxy_id = NULL;
  }

  if (basedrm->proxy_pw) {
    g_free ((gpointer) basedrm->proxy_pw);
    basedrm->proxy_pw = NULL;
  }

  if (drm_system_info->system_ids) {
    g_list_free_full (drm_system_info->system_ids, g_free);
    drm_system_info->system_ids = NULL;
  }

  if (drm_system_info->soap_action) {
    g_free (drm_system_info->soap_action);
    drm_system_info->soap_action = NULL;
  }

  if (drm_system_info->xml_node_name) {
    g_free (drm_system_info->xml_node_name);
    drm_system_info->xml_node_name = NULL;
  }

  if (drm_system_info->drmclient_id) {
    g_free (drm_system_info->drmclient_id);
    drm_system_info->drmclient_id = NULL;
  }

  if (drm_system_info->license_type) {
    g_free (drm_system_info->license_type);
    drm_system_info->license_type = NULL;
  }

  if (drm_system_info->content_id) {
    g_free (drm_system_info->content_id);
    drm_system_info->content_id = NULL;
  }

  if (drm_system_info->license_url) {
    g_free (drm_system_info->license_url);
    drm_system_info->license_url = NULL;
  }

  if (drm_system_info->group_license_url) {
    g_free (drm_system_info->group_license_url);
    drm_system_info->group_license_url = NULL;
  }

  rights_error_info->error_state = RIGHTS_ERROR_NONE;
  g_free (rights_error_info->content_id);
  g_free (rights_error_info->drm_system_id);
  g_free (rights_error_info->rights_issuer_url);

  return TRUE;
}

/* filter out the audio and video related fields from the up-stream caps,
   because they are not relevant to the input caps of this element and
   can cause caps negotiation failures with adaptive bitrate streams */
static void
gst_basedrm_remove_codec_fields (GstStructure * fields)
{
  gint j, n_fields = gst_structure_n_fields (fields);
  for (j = n_fields - 1; j >= 0; --j) {
    const gchar *field_name;

    field_name = gst_structure_nth_field_name (fields, j);
    if (g_strcmp0 (field_name, "base-profile") == 0 ||
        g_strcmp0 (field_name, "codec_data") == 0 ||
        g_strcmp0 (field_name, "height") == 0 ||
        g_strcmp0 (field_name, "framerate") == 0 ||
        g_strcmp0 (field_name, "level") == 0 ||
        g_strcmp0 (field_name, "pixel-aspect-ratio") == 0 ||
        g_strcmp0 (field_name, "profile") == 0 ||
        g_strcmp0 (field_name, "rate") == 0 ||
        g_strcmp0 (field_name, "width") == 0) {
      gst_structure_remove_field (fields, field_name);
    }
  }
}

/*
  Find out that what caps are allowed on the other pad in this element
  when pad, its direction and the caps are given
*/
static GstCaps *
gst_basedrm_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstBaseDrm *basedrm = GST_BASEDRM (base);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstCaps *new_caps = NULL;
  gint i, j;
  const GValue *value;
  guint32 width = 0;
  guint32 height = 0;

  g_return_val_if_fail (direction != GST_PAD_UNKNOWN, NULL);
  new_caps = gst_caps_new_empty ();

  /* Call once to get drm system info */
  if (!drm_system_info->system_ids) {
    if (basedrm_class->get_drm_info (basedrm, drm_system_info) == FALSE) {
      GST_ERROR_OBJECT (basedrm, "Failed to get DRM info");
      return NULL;
    }
    GST_DEBUG_OBJECT (basedrm, "DRM info received");
  }

  GST_DEBUG_OBJECT (basedrm,
      "direction: %s   caps: %" GST_PTR_FORMAT "   filter:" " %" GST_PTR_FORMAT,
      (direction == GST_PAD_SRC) ? "Src" : "Sink", caps, filter);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *in = gst_caps_get_structure (caps, i);
    GstStructure *out = NULL;
    gboolean duplicate = FALSE;

    if (direction == GST_PAD_SINK) {
      gint n_fields;

      if (!gst_structure_has_field (in, "original-media-type"))
        continue;

      out = gst_structure_copy (in);
      n_fields = gst_structure_n_fields (in);

      gst_structure_set_name (out,
          gst_structure_get_string (out, "original-media-type"));

      if (gst_structure_has_field (in, "width")) {
        value = gst_structure_get_value (in, "width");
        width = g_value_get_int (value);
      }

      if (gst_structure_has_field (in, "height")) {
        value = gst_structure_get_value (in, "height");
        height = g_value_get_int (value);
      }

      if (width != 0 && height != 0) {
        GST_DEBUG_OBJECT (basedrm, "width [%d] height[%d]", width, height);
        basedrm_class->set_video_info (basedrm, width, height);
      }

      /* filter out the DRM related fields from the down-stream caps */
      for (j = 0; j < n_fields; ++j) {
        const gchar *field_name;

        field_name = gst_structure_nth_field_name (in, j);

        if (g_str_has_prefix (field_name, "protection-system") ||
            g_str_has_prefix (field_name, "original-media-type")) {
          gst_structure_remove_field (out, field_name);
        }
      }
      duplicate = gst_caps_is_subset_structure (new_caps, out);
      if (g_str_has_prefix (gst_structure_get_name (out), "audio")) {
        GST_DEBUG_OBJECT (basedrm, "No set svp for audio");
        drm_system_info->is_svp = FALSE;
      }
      if (!duplicate) {
        gst_caps_append_structure (new_caps, out);
      } else {
        gst_structure_free (out);
      }
    } else {                    /* GST_PAD_SRC */
      /* filter out the video related fields from the up-stream caps,
         because they are not relevant to the input caps of this element and
         can cause caps negotiation failures with adaptive bitrate streams */
      GList *system_ids = drm_system_info->system_ids;
      while (system_ids) {
        out = gst_structure_copy (in);
        gst_basedrm_remove_codec_fields (out);
        gst_structure_set (out,
            "protection-system", G_TYPE_STRING,
            system_ids->data, "original-media-type",
            G_TYPE_STRING, gst_structure_get_name (in), NULL);
        gst_structure_set_name (out, "application/x-cenc");
        duplicate = gst_caps_is_subset_structure (new_caps, out);
        if (!duplicate) {
          gst_caps_append_structure (new_caps, out);
        } else {
          gst_structure_free (out);
        }
        system_ids = g_list_next (system_ids);
      }
    }
  }

  if (filter) {
    GstCaps *intersection;

    GST_DEBUG_OBJECT (basedrm, "Using filter caps %" GST_PTR_FORMAT, filter);
    intersection =
        gst_caps_intersect_full (new_caps, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (new_caps);
    new_caps = intersection;
  }

  GST_DEBUG_OBJECT (basedrm, "returning %" GST_PTR_FORMAT, new_caps);
  return new_caps;
}

/* convert a DRM SystemID (UUID) in bytes representation to string representation */
static gchar *
gst_basedrm_uuid_bytes_to_string (const guint8 * uuid_bytes)
{
  gsize uuid_string_length = UUID_STRING_LEN + 1;
  gchar *uuid_string = (gchar *) g_malloc0 (uuid_string_length);

  g_snprintf (uuid_string, uuid_string_length,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
      "%02x%02x-%02x%02x%02x%02x%02x%02x",
      uuid_bytes[0], uuid_bytes[1], uuid_bytes[2], uuid_bytes[3],
      uuid_bytes[4], uuid_bytes[5], uuid_bytes[6], uuid_bytes[7],
      uuid_bytes[8], uuid_bytes[9], uuid_bytes[10], uuid_bytes[11],
      uuid_bytes[12], uuid_bytes[13], uuid_bytes[14], uuid_bytes[15]);

  return uuid_string;
}

static gboolean
gst_basedrm_decrypt_init (GstBaseDrm * basedrm, GBytes * iv)
{
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  guint8 *iv_tab;
  gsize iv_size;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (basedrm, "decrypt_init");

  iv_tab = (guint8 *) g_bytes_get_data (iv, &iv_size);
  if (basedrm_class->set_iv (basedrm, iv_tab, iv_size) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "set IV problem");
    res = FALSE;
  }

  if (basedrm_class->prepare_decrypt (basedrm) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to prepare decrypt context");
    res = FALSE;
  }

  return res;
}

/* encrypted buffer must have a CENC encrypted sample
 * and protectiom metadata required for in place decryption
 * of encrypted buffer */
static GstFlowReturn
gst_basedrm_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstBaseDrm *basedrm = GST_BASEDRM (base);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDecryptInfo *decrypt_info = &basedrm->decrypt_info;
  GstFlowReturn ret = GST_FLOW_NOT_SUPPORTED;
  GstMapInfo map, iv_map, subsamples_map, kid_map;
  const GstProtectionMeta *prot_meta = NULL;
  guint subsample_count = 0;
  const GValue *value;
  GstBuffer *iv_buf = NULL;
  GBytes *iv_bytes = NULL;
  guint iv_size;
  GstBuffer *subsamples_buf = NULL;
  GstByteReader *reader = NULL;
  gboolean encrypted;
  GstBuffer *kid_buf = NULL;
  GstByteWriter bw;
  GstBuffer *key_info = NULL;
  guint32 *subsample_info = NULL;
  guint32 i;

  GST_LOG_OBJECT (basedrm, "decrypt in-place");

  if (!buf) {
    GST_ERROR_OBJECT (basedrm, "Failed to get writable buffer");
    return GST_FLOW_ERROR;
  }

  prot_meta = (GstProtectionMeta *) gst_buffer_get_protection_meta (buf);
  if (!prot_meta) {
    GST_ERROR_OBJECT (basedrm,
        "Failed to get GstProtection metadata from buffer");
    goto out;
  }

  if (!gst_structure_get_boolean (prot_meta->info, "encrypted", &encrypted)) {
    GST_ERROR_OBJECT (basedrm, "failed to get encrypted flag");
    goto release;
  }

  /* bypass decryption if sample is not encrypted */
  if (!encrypted) {
    ret = GST_FLOW_OK;
    goto release;
  }

  GST_DEBUG_OBJECT (base, "protection meta: %" GST_PTR_FORMAT, prot_meta->info);
  if (!gst_structure_get_uint (prot_meta->info, "iv_size", &iv_size)) {
    GST_ERROR_OBJECT (basedrm, "failed to get iv_size");
    goto release;
  }

  if (iv_size == 0) {
    if (!gst_structure_get_uint (prot_meta->info, "constant_iv_size", &iv_size)) {
      GST_ERROR_OBJECT (basedrm, "failed to get constant_iv_size");
      goto release;
    }
    value = gst_structure_get_value (prot_meta->info, "constant_iv");
    if (!value) {
      GST_ERROR_OBJECT (basedrm, "Failed to get constant_iv for sampe");
      goto release;
    }
  } else {
    value = gst_structure_get_value (prot_meta->info, "iv");
    if (!value) {
      GST_ERROR_OBJECT (basedrm, "Failed to get IV for sample");
      goto release;
    }
  }
  iv_buf = gst_value_get_buffer (value);
  if (!gst_buffer_map (iv_buf, &iv_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (basedrm, "Failed to map IV");
    goto release;
  }
  iv_bytes = g_bytes_new (iv_map.data, iv_map.size);
  gst_buffer_unmap (iv_buf, &iv_map);

  if (!gst_structure_get_uint (prot_meta->info, "subsample_count",
          &subsample_count)) {
    GST_ERROR_OBJECT (basedrm, "failed to get subsample_count");
    goto release;
  }

  /* get Key ID for contetnt */
  value = gst_structure_get_value (prot_meta->info, "kid");
  if (value) {
    kid_buf = gst_value_get_buffer (value);
    if (!gst_buffer_map (kid_buf, &kid_map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (basedrm, "Failed to map kid");
      goto release;
    }
    basedrm_class->set_kid (basedrm, kid_map.data, kid_map.size);
    gst_buffer_unmap (kid_buf, &kid_map);
  }

  if (subsample_count) {
    value = gst_structure_get_value (prot_meta->info, "subsamples");
    if (!value) {
      GST_ERROR_OBJECT (basedrm, "Failed to get subsamples");
      goto release;
    }
    subsamples_buf = gst_value_get_buffer (value);
    if (!gst_buffer_map (subsamples_buf, &subsamples_map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (basedrm, "Failed to map subsample buffer");
      goto release;
    }
    reader = gst_byte_reader_new (subsamples_map.data, subsamples_map.size);
    if (!reader) {
      GST_ERROR_OBJECT (basedrm, "Failed to allocate subsample reader");
      goto release;
    }
  }

  if (!gst_structure_get_uint (prot_meta->info, "scheme_type",
          &decrypt_info->scheme_type)) {
    GST_WARNING_OBJECT (basedrm, "Failed to get protection scheme type");
  }

  if (decrypt_info->scheme_type == FOURCC_cens
      || decrypt_info->scheme_type == FOURCC_cbcs) {
    if (!gst_structure_get_uint (prot_meta->info, "crypt_byte_block",
            &decrypt_info->crypt_byte_block)) {
      GST_ERROR_OBJECT (basedrm, "Failed to get crypt_byte_block");
      goto release;
    }

    if (!gst_structure_get_uint (prot_meta->info, "skip_byte_block",
            &decrypt_info->skip_byte_block)) {
      GST_ERROR_OBJECT (basedrm, "Failed to get skip_byte_block");
      goto release;
    }
  }

  if (gst_basedrm_decrypt_init (basedrm, iv_bytes) == FALSE)
    goto release;

  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (basedrm, "Failed to map buffer");
    goto release;
  }

  GST_LOG_OBJECT (basedrm, "decrypt sample %" G_GSIZE_FORMAT, map.size);

  decrypt_info->data = map.data;
  decrypt_info->data_size = map.size;

  decrypt_info->subsample_count = subsample_count ? subsample_count : 1;
  subsample_info =
      (guint32 *) g_malloc0 (sizeof (guint32) * decrypt_info->subsample_count *
      2);
  decrypt_info->subsample_info = subsample_info;

  subsample_info[0] = 0;
  subsample_info[1] = map.size;

  for (i = 0; i < subsample_count * 2; i += 2) {
    guint16 n_bytes_clear = 0;
    guint32 n_bytes_encrypted = 0;
    if (!gst_byte_reader_get_uint16_be (reader, &n_bytes_clear)
        || !gst_byte_reader_get_uint32_be (reader, &n_bytes_encrypted)) {
      goto beach;
    }
    subsample_info[i] = (guint32) n_bytes_clear;
    subsample_info[i + 1] = n_bytes_encrypted;
  }

  if (!drm_system_info->is_svp) {
    g_mutex_lock (&basedrm_mutex);
    if (basedrm_class->decrypt (basedrm, decrypt_info) == FALSE) {
      g_mutex_unlock (&basedrm_mutex);
      goto beach;
    }
    g_mutex_unlock (&basedrm_mutex);
  } else {
    GstBuffer *inband_buf;
    guint32 key_info_size = 0;

    const guint8 tag[4] = { 'i', 'b', 'p', 'i' };
    guint32 segment_count = decrypt_info->subsample_count * 2;

    gst_byte_writer_init (&bw);
    if (!gst_byte_writer_put_data (&bw, tag, sizeof (tag))) {
      GST_ERROR_OBJECT (basedrm, "Failed to put tag");
      goto beach;
    }

    if (!gst_byte_writer_put_uint32_le (&bw, segment_count)) {
      GST_ERROR_OBJECT (basedrm, "Failed to put segment_count");
      goto beach;
    }

    for (i = 0; i < decrypt_info->subsample_count * 2; i += 2) {
      guint32 n_bytes_clear = subsample_info[i];
      guint32 n_bytes_encrypted = subsample_info[i + 1];
      if (!gst_byte_writer_put_uint32_le (&bw, n_bytes_clear)) {
        GST_ERROR_OBJECT (basedrm, "Failed to put n_bytes_clear");
        goto beach;
      }
      if (!gst_byte_writer_put_uint32_le (&bw, 0x80000000 | n_bytes_encrypted)) {
        GST_ERROR_OBJECT (basedrm, "Failed to put n_bytes_encrypted");
        goto beach;
      }
    }

    key_info = basedrm_class->get_key_info (basedrm);
    if (key_info)
      key_info_size = gst_buffer_get_size (key_info);

    if (decrypt_info->scheme_type == FOURCC_cens
        || decrypt_info->scheme_type == FOURCC_cbcs) {
      key_info_size += sizeof (guint32);        /* crypt_byte_block */
      key_info_size += sizeof (guint32);        /* skip_byte_block */
    }

    if (!gst_byte_writer_put_uint32_le (&bw, gst_buffer_get_size (iv_buf))) {
      GST_ERROR_OBJECT (basedrm, "Failed to put iv_buf size");
      goto beach;
    }
    if (!gst_byte_writer_put_buffer (&bw, iv_buf, 0, -1)) {
      GST_ERROR_OBJECT (basedrm, "Failed to put iv_buf");
      goto beach;
    }

    if (!gst_byte_writer_put_uint32_le (&bw, key_info_size)) {
      GST_ERROR_OBJECT (basedrm, "Failed to put key_info_size");
      goto beach;
    }
    if (!gst_byte_writer_put_buffer (&bw, key_info, 0, -1)) {
      GST_ERROR_OBJECT (basedrm, "Failed to put kid_buf");
      goto beach;
    }

    if (decrypt_info->scheme_type == FOURCC_cens
        || decrypt_info->scheme_type == FOURCC_cbcs) {
      if (!gst_byte_writer_put_uint32_le (&bw, decrypt_info->crypt_byte_block)) {
        GST_ERROR_OBJECT (basedrm, "Failed to put crypt_byte_block");
        goto beach;
      }
      if (!gst_byte_writer_put_uint32_le (&bw, decrypt_info->skip_byte_block)) {
        GST_ERROR_OBJECT (basedrm, "Failed to put skip_byte_block");
        goto beach;
      }
    }
    inband_buf = gst_byte_writer_reset_and_get_buffer (&bw);
    gst_buffer_prepend_memory (buf, gst_buffer_get_all_memory (inband_buf));
    gst_buffer_unref (inband_buf);
  }
  ret = GST_FLOW_OK;

beach:
  gst_buffer_unmap (buf, &map);
release:
  if (reader) {
    gst_byte_reader_free (reader);
  }
  if (subsamples_buf) {
    gst_buffer_unmap (subsamples_buf, &subsamples_map);
  }
  if (prot_meta) {
    gst_buffer_remove_meta (buf, (GstMeta *) prot_meta);
  }
  if (iv_bytes) {
    g_bytes_unref (iv_bytes);
  }
  if (key_info) {
    gst_buffer_unref (key_info);
  }
  g_free (subsample_info);
out:
  return ret;
}

static void
gst_basedrm_drm_license_free (GstDRMLicenseInfo * drm_license_info)
{
  if (drm_license_info->url) {
    g_free (drm_license_info->url);
    drm_license_info->url = NULL;
  }
  if (drm_license_info->challenge) {
    g_free (drm_license_info->challenge);
    drm_license_info->challenge = NULL;
  }
  if (drm_license_info->response) {
    g_free (drm_license_info->response);
    drm_license_info->response = NULL;
  }
}

static gboolean
gst_basedrm_acquire_license (GstBaseDrm * basedrm)
{
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstDRMLicenseInfo *drm_license_info = &basedrm->drm_license_info;
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (basedrm, "acquire_license");

  if (basedrm_class->get_license_challenge (basedrm, drm_license_info) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to get license challenge");
    goto Error;
  }

  if (basedrm_class->request_license (basedrm, drm_license_info) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to request license");
    goto Error;
  }

  if (basedrm_class->store_license (basedrm, drm_license_info) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to store license");
    goto Error;
  }

  res = TRUE;

Error:
  /* free all license info, acquire license may be called again */
  gst_basedrm_drm_license_free (drm_license_info);
  return res;
}

static gboolean
gst_basedrm_setup_license (GstBaseDrm * basedrm, guint8 * data, guint len)
{
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;
  gboolean ret = FALSE;

  g_mutex_lock (&basedrm_mutex);
  GST_DEBUG_OBJECT (basedrm, "drm license setup");

  rights_error_info->error_state = RIGHTS_ERROR_NO_LICENSE;

  /* set the DRM Header Object */
  if (basedrm_class->drm_init (basedrm, data, len) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to do drm init");
    goto Error;
  }

  /* check if playback is already allowed due to previous set license
   * if TRUE then do not go for license acquisition etc. */
  if (basedrm_class->is_playback_allowed (basedrm) == TRUE) {
    GST_DEBUG_OBJECT (basedrm, "playback is allowed");
    goto exit;
  }

  if (gst_basedrm_acquire_license (basedrm) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to acquire license");
    goto Error;
  }

exit:
  rights_error_info->error_state = RIGHTS_ERROR_NONE;
  ret = TRUE;

Error:
  gst_basedrm_notify_rights_error_info (basedrm);
  g_mutex_unlock (&basedrm_mutex);
  return ret;
}

/* Parse Protection System Specific Header (pssh),
 * it carries Protection System Specific Information (pssi)
 * e.g. DRM header objectm, Key ID */
static gboolean
gst_basedrm_parse_pssh_box (GstBaseDrm * basedrm, GstBuffer * pssh,
    guint8 ** data, guint * data_size)
{
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstMapInfo info;
  GstByteReader br;
  guint8 version;
  gboolean ret = FALSE;
  GstStructure *encrypted_structure;
  GstMessage *encrypted_message;

  encrypted_structure = gst_structure_new ("encrypted",
      "init-data-type", G_TYPE_STRING, "cenc",
      "init-data", GST_TYPE_BUFFER, pssh, NULL);

  GST_DEBUG_OBJECT (basedrm,
      "Posting encrypted message to application: %" GST_PTR_FORMAT,
      encrypted_structure);

  encrypted_message = gst_message_new_element (GST_OBJECT_CAST (basedrm),
      encrypted_structure);
  gst_element_post_message (GST_ELEMENT_CAST (basedrm), encrypted_message);

  gst_buffer_map (pssh, &info, GST_MAP_READ);
  gst_byte_reader_init (&br, info.data, info.size);

  /* skip box size (4 bytes) and box type (4 bytes) */
  gst_byte_reader_skip_unchecked (&br, 8);
  version = gst_byte_reader_get_uint8_unchecked (&br);
  GST_DEBUG_OBJECT (basedrm, "pssh version: %u", version);

  /* skip box flags (3 bytes) and UUID of the content protection system (16 bytes) */
  gst_byte_reader_skip_unchecked (&br, 19);

  if (version > 0) {
    /* Parse KeyIDs */
    guint32 key_id_count = 0;
    const guint8 *key_id_data = NULL;
    const guint key_id_size = KEY_ID_SIZE;

    key_id_count = gst_byte_reader_get_uint32_be_unchecked (&br);
    GST_DEBUG_OBJECT (basedrm, "there are %u key IDs", key_id_count);
    key_id_data = gst_byte_reader_get_data_unchecked (&br, key_id_count * 16);

    while (key_id_count > 0) {
      gchar *key_id_string = gst_basedrm_uuid_bytes_to_string (key_id_data);
      GST_DEBUG_OBJECT (basedrm, "key_id: %s", key_id_string);
      g_free (key_id_string);
      key_id_data += key_id_size;
      --key_id_count;
    }
  }

  /* Parse Data */
  *data_size = gst_byte_reader_get_uint32_be_unchecked (&br);
  GST_DEBUG_OBJECT (basedrm, "pssh data size: %u", *data_size);
  *data = (guint8 *) gst_byte_reader_get_data_unchecked (&br, *data_size);

  basedrm_class->restore_original_pssh (basedrm, info.data, info.size, data,
      data_size);

  ret = TRUE;

beach:
  gst_buffer_unmap (pssh, &info);
  return ret;
}

static void
gst_basedrm_free_xml_data (GstContentProtection * content_protection_data,
    GstDRMSystemInfo * drm_system_info)
{
  if (content_protection_data) {
    if (content_protection_data->schemeIdUri)
      xmlFree (content_protection_data->schemeIdUri);
    if (content_protection_data->value)
      xmlFree (content_protection_data->value);
    if (content_protection_data->KID)
      xmlFree (content_protection_data->KID);
    if (content_protection_data->data)
      xmlFree (content_protection_data->data);
    g_free (content_protection_data);
  }

  if (drm_system_info->license_type) {
    xmlFree (drm_system_info->license_type);
    drm_system_info->license_type = NULL;
  }
  if (drm_system_info->content_id) {
    xmlFree (drm_system_info->content_id);
    drm_system_info->content_id = NULL;
  }
  if (drm_system_info->license_url) {
    xmlFree (drm_system_info->license_url);
    drm_system_info->license_url = NULL;
  }
  if (drm_system_info->group_license_url) {
    xmlFree (drm_system_info->group_license_url);
    drm_system_info->group_license_url = NULL;
  }
}

static gboolean
gst_basedrm_get_xml_node_content (xmlNode * a_node, gchar ** content)
{
  xmlChar *node_content = NULL;
  gboolean exists = FALSE;

  node_content = xmlNodeGetContent (a_node);
  if (node_content) {
    exists = TRUE;
    *content = (gchar *) node_content;
    GST_LOG (" - %s: %s", a_node->name, *content);
  }

  return exists;
}

static gboolean
gst_basedrm_get_xml_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    *property_value = (gchar *) prop_string;
    exists = TRUE;
    GST_LOG (" - %s: %s", property_name, prop_string);
  }

  return exists;
}

static gboolean
gst_basedrm_get_xml_node_as_string (xmlNode * a_node, gchar ** content)
{
  gboolean exists = FALSE;
  const char *txt_encoding;
  xmlOutputBufferPtr out_buf;

  txt_encoding = (const char *) a_node->doc->encoding;
  out_buf = xmlAllocOutputBuffer (NULL);
  g_assert (out_buf != NULL);
  xmlNodeDumpOutput (out_buf, a_node->doc, a_node, 0, 0, txt_encoding);
  xmlOutputBufferFlush (out_buf);
#ifdef LIBXML2_NEW_BUFFER
  if (xmlOutputBufferGetSize (out_buf) > 0) {
    *content =
        (gchar *) xmlStrndup (xmlOutputBufferGetContent (out_buf),
        xmlOutputBufferGetSize (out_buf));
    exists = TRUE;
  }
#else
  if (out_buf->conv && out_buf->conv->use > 0) {
    *content =
        (gchar *) xmlStrndup (out_buf->conv->content, out_buf->conv->use);
    exists = TRUE;
  } else if (out_buf->buffer && out_buf->buffer->use > 0) {
    *content =
        (gchar *) xmlStrndup (out_buf->buffer->content, out_buf->buffer->use);
    exists = TRUE;
  }
#endif // LIBXML2_NEW_BUFFER
  (void) xmlOutputBufferClose (out_buf);

  if (exists) {
    GST_LOG (" - %s: %s", a_node->name, *content);
  }
  return exists;
}

static gboolean
gst_basedrm_validate_system_id (GstBaseDrm * basedrm, const gchar * system_id)
{
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GList *system_ids = drm_system_info->system_ids;
  while (system_ids) {
    if (g_ascii_strncasecmp ((const gchar *) system_ids->data, system_id,
            UUID_STRING_LEN) == 0)
      return TRUE;
    system_ids = g_list_next (system_ids);
  }
  return FALSE;
}

/* parse content protection element from DASH demux */
static gboolean
gst_basedrm_parse_content_protection_element (GstBaseDrm * basedrm,
    GstBuffer * pssi)
{
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_GET_CLASS (basedrm);
  GstContentProtection *content_protection_data = NULL;
  GstMapInfo info;
  xmlDocPtr doc;
  xmlNode *root_element = NULL, *cur_node = NULL;
  gboolean ret = FALSE;
  guint8 *decodec_data = NULL;
  guint8 *init_data = NULL;
  guint init_data_len = 0;
  guint data_len = 0;
  gchar *drmclient_id = NULL;
  gsize drmclient_id_len = 0;
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstBuffer *buffer = NULL;

  GST_DEBUG_OBJECT (basedrm, "Content Protection element from DASH demux");

  gst_buffer_map (pssi, &info, GST_MAP_READ);

  GST_DEBUG_OBJECT (basedrm, "ContentProtection tag:\n %s", info.data);
  /* FIXME: drmclient_id is temporarily appended after ContentProtection
   * xml element for proactive license acquisition support. Following codes
   * will be unnecessary if drmclient_id is passed as a xml element in
   * ContentProtection xml element. */
  if (info.data[info.size - 1] != '>') {
    drmclient_id = g_strrstr_len ((const char *) info.data, info.size, ">") + 1;
    drmclient_id_len = info.data + info.size - (guint8 *) drmclient_id;
    g_free (drm_system_info->drmclient_id);
    drm_system_info->drmclient_id = g_strndup ((const char *) drmclient_id,
        drmclient_id_len);
    GST_DEBUG_OBJECT (basedrm, "drmclient_id = %s",
        drm_system_info->drmclient_id);
  }
  /* this initialize the library and check potential ABI mismatches
   * between the version it was compiled for and the actual shared
   * library used
   */
  LIBXML_TEST_VERSION
      /* parse "data" into a document (which is a libxml2 tree structure xmlDoc) */
      doc = xmlReadMemory ((const char *) info.data,
      info.size - drmclient_id_len, "ContentProtection.xml", NULL, 0);

  if (!doc) {
    GST_ERROR_OBJECT (basedrm, "Failed to parse XML from pssi event");
    goto beach;
  }
  root_element = xmlDocGetRootElement (doc);

  if (root_element->type != XML_ELEMENT_NODE
      || xmlStrcmp (root_element->name, (xmlChar *) "ContentProtection") != 0) {
    GST_ERROR_OBJECT (basedrm, "Failed to find ContentProtection element");
    goto beach;
  }

  content_protection_data =
      (GstContentProtection *) g_malloc0 (sizeof (GstContentProtection));
  GST_LOG ("attributes of %s node:", root_element->name);
  gst_basedrm_get_xml_prop_string (root_element, "schemeIdUri",
      &content_protection_data->schemeIdUri);
  if (!gst_basedrm_validate_system_id (basedrm,
          content_protection_data->schemeIdUri + strlen ("urn:uuid:"))) {
    GST_ERROR_OBJECT (basedrm, "invalid schemeIdUri");
    goto beach;
  }

  gst_basedrm_get_xml_prop_string (root_element, "value",
      &content_protection_data->value);
  gst_basedrm_get_xml_prop_string (root_element, "cenc:default_KID",
      &content_protection_data->KID);
  GST_DEBUG_OBJECT (basedrm, "cenc:default_KID = %s",
      content_protection_data->KID);

  for (cur_node = root_element->children; cur_node; cur_node = cur_node->next) {
    gboolean xmlns_cenc_used = FALSE;
    gboolean xmlns_dashif_used = FALSE;

    if (cur_node->type != XML_ELEMENT_NODE)
      continue;

    /* Check whether current node has xmlns:cenc or xmlns:dashif or not. */
    for (xmlNs * xmlns = cur_node->ns; xmlns; xmlns = xmlns->next) {
      GST_DEBUG_OBJECT (basedrm, "xmlns->prefix: %s", xmlns->prefix);
      if (!xmlns->prefix)
        continue;
      if (xmlStrEqual (xmlns->prefix, (const xmlChar *) "cenc")) {
        xmlns_cenc_used = TRUE;
      } else if (xmlStrEqual (xmlns->prefix, (const xmlChar *) "dashif")) {
        xmlns_dashif_used = TRUE;
      }
    }

    /* If current node has xmlns:cenc, cur_node->name may be just "pssh". */
    if ((xmlns_cenc_used && xmlStrEqual (cur_node->name, (xmlChar *) "pssh")) ||
        xmlStrEqual (cur_node->name, (xmlChar *) "cenc:pssh")) {
      gst_basedrm_get_xml_node_content (cur_node,
          &content_protection_data->data);
      if (!content_protection_data->data) {
        GST_ERROR_OBJECT (basedrm, "cenc:pssh value not present");
        goto beach;
      }

      GST_DEBUG_OBJECT (basedrm, "cenc:pssh = %s",
          content_protection_data->data);

      g_free (decodec_data);

      decodec_data =
          (guint8 *) g_base64_decode (content_protection_data->data,
          (gsize *) & data_len);

      if (!decodec_data) {
        GST_ERROR_OBJECT (basedrm, "invalid base64-encoded pssh box");
        goto beach;
      }

      buffer =
          gst_buffer_new_wrapped (g_memdup (decodec_data, data_len), data_len);

      if (gst_basedrm_parse_pssh_box (basedrm, buffer, &init_data,
              &init_data_len) == FALSE) {
        GST_ERROR_OBJECT (basedrm, "Failed to parse pssh box");
        goto beach;
      }
    } else if ((xmlns_dashif_used
            && xmlStrEqual (cur_node->name, (xmlChar *) "Laurl"))
        || xmlStrEqual (cur_node->name, (xmlChar *) "dashif:Laurl")) {

      GST_DEBUG_OBJECT (basedrm, "xmlns_dashif_used: %s , cur_node->name: %s",
          xmlns_dashif_used ? "true" : "false", cur_node->name);
      gst_basedrm_get_xml_prop_string (cur_node, "licenseType",
          &drm_system_info->license_type);

      if (!drm_system_info->license_type) {
        GST_DEBUG_OBJECT (basedrm,
            "dashif:Laurl licenseType value doesn't exist");
        continue;
      }

      GST_DEBUG_OBJECT (basedrm, "licenseType: %s",
          drm_system_info->license_type);

      if (g_strcmp0 (drm_system_info->license_type, "contentId-1.0") == 0) {
        gst_basedrm_get_xml_node_content (cur_node,
            &drm_system_info->content_id);
        GST_DEBUG_OBJECT (basedrm, "URL: %s", drm_system_info->content_id);
      } else if (g_strcmp0 (drm_system_info->license_type, "license-1.0") == 0) {
        gst_basedrm_get_xml_node_content (cur_node,
            &drm_system_info->license_url);
        GST_DEBUG_OBJECT (basedrm, "URL: %s", drm_system_info->license_type);
      } else if (g_strcmp0 (drm_system_info->license_type,
              "groupLicense-1.0") == 0) {
        gst_basedrm_get_xml_node_content (cur_node,
            &drm_system_info->group_license_url);
        GST_DEBUG_OBJECT (basedrm, "URL: %s",
            drm_system_info->group_license_url);
      }
    } else {
      gboolean ignore = TRUE;
      guint8 *pssi_data = NULL;
      guint pssi_data_len = 0;

      /* for custom pssi usage (e.g., playready mspr:pro, ..) */
      gst_basedrm_get_xml_node_as_string (cur_node,
          &content_protection_data->data);
      GST_DEBUG_OBJECT (basedrm, "custom pssi = %s",
          content_protection_data->data);

      if (basedrm_class->resolve_custom_pssi (basedrm,
              content_protection_data->data, &pssi_data,
              &pssi_data_len, &ignore) == FALSE) {
        GST_ERROR_OBJECT (basedrm, "Failed to resolve custom pssi");
        goto beach;
      }

      /* if xml node has content protection box, ignore flag is changed to FALSE, */
      /* and init_data will reference current node's data */
      if (!ignore && pssi_data != NULL) {
        g_free (decodec_data);
        init_data = decodec_data = pssi_data;
        init_data_len = pssi_data_len;
        break;
      }
      GST_DEBUG_OBJECT (basedrm, "ignore custom pssi: %s",
          content_protection_data->data);
      g_free (pssi_data);
    }
  }

  if (init_data != NULL &&
      gst_basedrm_setup_license (basedrm, init_data, init_data_len) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to setup license");
    goto beach;
  }

  ret = TRUE;

beach:
  if (buffer) {
    gst_buffer_unref (buffer);
  }
  g_free (decodec_data);
  gst_basedrm_free_xml_data (content_protection_data, drm_system_info);
  if (doc)
    xmlFreeDoc (doc);
  gst_buffer_unmap (pssi, &info);
  return (ret);
}

static gboolean
gst_basedrm_sink_event_handler (GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret = FALSE;
  const gchar *system_id;
  GstBuffer *pssi = NULL;
  const gchar *loc;
  guint8 *data;
  guint data_size;
  GstBaseDrm *basedrm = GST_BASEDRM (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_PROTECTION:
      GST_DEBUG_OBJECT (basedrm, "received protection event");
      gst_event_parse_protection (event, &system_id, &pssi, &loc);
      if (!gst_basedrm_validate_system_id (basedrm, system_id)) {
        GST_WARNING_OBJECT (basedrm, "system ID(%s) does not match", system_id);
        gst_event_unref (event);
        ret = TRUE;
        goto out;
      }
      GST_DEBUG_OBJECT (basedrm, "system_id: %s", system_id);
      if (!pssi) {
        GST_ERROR_OBJECT (basedrm, "pssi NULL");
        gst_event_unref (event);
        goto out;
      }
      if (g_ascii_strncasecmp (loc, "dash/mpd", 8) == 0) {
        GST_DEBUG_OBJECT (basedrm, "event carries MPD pssi data");
        ret = gst_basedrm_parse_content_protection_element (basedrm, pssi);
      } else if (g_str_has_prefix (loc, "isobmff/")) {
        GST_DEBUG_OBJECT (basedrm, "event carries pssh data from qtdemux");
        ret = gst_basedrm_parse_pssh_box (basedrm, pssi, &data, &data_size);
        if (ret) {
          ret = gst_basedrm_setup_license (basedrm, data, data_size);
        }
      } else if (g_str_has_prefix (loc, "smooth-streaming")) {
        GST_DEBUG_OBJECT (basedrm,
            "event carries protection header from mssdemux");
        ret = gst_basedrm_parse_protection_header (basedrm, pssi);
      } else if (g_str_has_prefix (loc, "hls-streaming")) {
        GST_DEBUG_OBJECT (basedrm,
            "event carries protection header from hlsdemux");
        ret = gst_basedrm_parse_protection_header (basedrm, pssi);
      }
      gst_event_unref (event);
      break;
    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
      break;
  }

out:
  return ret;
}

static gboolean
gst_basedrm_parse_attributes (gchar ** ptr, gchar ** a, gchar ** v)
{
  gchar *end = NULL, *p, *ve;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (*ptr != NULL, FALSE);
  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (v != NULL, FALSE);

  *a = *ptr;
  end = p = g_utf8_strchr (*ptr, -1, ',');
  if (end) {
    gchar *q = g_utf8_strchr (*ptr, -1, '"');
    if (q && q < end) {
      q = g_utf8_next_char (q);
      if (q) {
        q = g_utf8_strchr (q, -1, '"');
      }
      if (q) {
        end = p = g_utf8_strchr (q, -1, ',');
      }
    }
    if (end) {
      do {
        end = g_utf8_next_char (end);
      } while (end && *end == ' ');
      *p = '\0';
    }
  }

  *v = p = g_utf8_strchr (*ptr, -1, '=');
  if (*v) {
    *p = '\0';
    *v = g_utf8_next_char (*v);
    if (**v == '"') {
      ve = g_utf8_next_char (*v);
      if (ve) {
        ve = g_utf8_strchr (*v, -1, '"');
      }
      if (ve) {
        *v = g_utf8_next_char (*v);
        *ve = '\0';
      } else {
        GST_WARNING ("Cannot remove quotation marks from %s", *a);
      }
    }
  } else {
    GST_WARNING ("missing = after attribute");
    return FALSE;
  }

  *ptr = end;
  return TRUE;
}

static void
gst_basedrm_uri_protocol_check_internal (const gchar * uri, gchar ** endptr)
{
  gchar *check = (gchar *) uri;

  g_assert (uri != NULL);
  g_assert (endptr != NULL);

  if (g_ascii_isalpha (*check)) {
    check++;
    while (g_ascii_isalnum (*check) || *check == '+'
        || *check == '-' || *check == '.')
      check++;
  }

  *endptr = check;
}

static gboolean
gst_basedrm_uri_is_valid (const gchar * uri)
{
  gchar *endptr;

  g_return_val_if_fail (uri != NULL, FALSE);

  gst_basedrm_uri_protocol_check_internal (uri, &endptr);

  return *endptr == ':' && ((gsize) (endptr - uri)) >= 2;
}

/* Parses a Protection Header Content for HLS & MSS */
static gboolean
gst_basedrm_parse_protection_header (GstBaseDrm * basedrm, GstBuffer * pssi)
{
  gboolean ret = FALSE;
  GstMapInfo info;
  guint8 *uri = NULL;
  guint length = 0;
  gchar *attribute, *value;

  gst_buffer_map (pssi, &info, GST_MAP_READ);

  while (info.data
      && gst_basedrm_parse_attributes ((gchar **) & info.data, &attribute,
          &value)) {
    if (g_str_equal (attribute, "URI")) {
      if (!gst_basedrm_uri_is_valid (value)) {
        goto beach;
      }
      length = strlen (value);
      uri = g_memdup (value, length);
      break;
    }
  }

  if (!uri || gst_basedrm_setup_license (basedrm, uri, length) == FALSE) {
    GST_ERROR_OBJECT (basedrm, "Failed to setup license");
    goto beach;
  }

  ret = TRUE;

beach:
  g_free (uri);
  gst_buffer_unmap (pssi, &info);
  return ret;
}

static void
gst_basedrm_notify_rights_error_info (GstBaseDrm * basedrm)
{
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;

  if (rights_error_info->error_state != RIGHTS_ERROR_NONE) {
    GstStructure *s = gst_structure_new ("drm-rights-error", "error-state",
        G_TYPE_UINT, rights_error_info->error_state, "content-id",
        G_TYPE_STRING, rights_error_info->content_id, "drm-system-id",
        G_TYPE_STRING, rights_error_info->drm_system_id, "rights-issuer-url",
        G_TYPE_STRING, rights_error_info->rights_issuer_url, NULL);

    GST_DEBUG_OBJECT (basedrm,
        "Posting drm-rights-error msg to application: %" GST_PTR_FORMAT, s);

    gst_element_post_message (GST_ELEMENT_CAST (basedrm),
        gst_message_new_element (GST_OBJECT_CAST (basedrm), s));
  }

  rights_error_info->error_state = RIGHTS_ERROR_NONE;
}
