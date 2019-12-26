/*
* This file is part of ClearKey DRM
*
* Copyright (C) 2015-2017, STMicroelectronics - All Rights Reserved
* Author(s): Jean-Christophe Trotin <jean-christophe.trotin@st.com> for STMicroelectronics.
*
* License terms: LGPL V2.1.
*
* ClearKey DRM is free software; you can redistribute it and/or modify it
* under the terms of the GNU Lesser General Public License version 2.1 as
* published by the Free Software Foundation.
*
* ClearKey DRM is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
* for more details.
*
* You should have received a copy of the GNU Lesser General Public License along
* with this library. If not, see <http://www.gnu.org/licenses/>.
*
*/

/**
 * SECTION:element-gstckdrm
 * Decrypts media that has been encrypted / protected using ClearKey DRM.
 * The "ClearKey" DRM uses plain-text clear (unencrypted) key(s) to decrypt
 * the sink data. No additional client-side content protection is required.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 playbin uri=http://html5.cablelabs.com:8100/cenc/ck/dash.mpd
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbytereader.h>
#include <gst/basedrm/gstbasedrm.h>
#include <glib.h>

#include "gstckdrm.h"
#include "gstaesctr.h"
#include "gstaescbcs.h"
#include "gstjwk.h"

#define GST_CAT_DEFAULT gst_ckdrm_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* UUID is System ID of ClearKey DRM system */
#define CKDRM_UUID "1077efec-c0b2-4d02-ace3-3c1e52e2fb4b"

enum
{
  PROP_0,
  PROP_KEY,
  PROP_JWK,
  NUM_PROPERTIES
};

/* prototypes */
static void gst_ckdrm_set_property (GObject *, guint prop_id,
    const GValue *, GParamSpec *);
static GstStateChangeReturn gst_ckdrm_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_ckdrm_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info);
static gboolean gst_ckdrm_start (GstBaseDrm * basedrm);
static gboolean gst_ckdrm_is_playback_allowed (GstBaseDrm * basedrm);
static gboolean gst_ckdrm_drm_init (GstBaseDrm * basedrm, guint8 * header,
    guint size);
static gboolean gst_ckdrm_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_ckdrm_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_ckdrm_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_ckdrm_prepare_decrypt (GstBaseDrm * basedrm);
static gboolean gst_ckdrm_set_kid (GstBaseDrm * basedrm, guint8 * kid_data,
    gsize kid_size);
static gboolean gst_ckdrm_set_iv (GstBaseDrm * basedrm, guint8 * iv_data,
    gsize iv_size);
static gboolean gst_ckdrm_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decryptInfo);
static gboolean gst_ckdrm_stop (GstBaseDrm * basedrm);
gboolean plugin_init (GstPlugin * plugin);

const char ckdrm_soap_action[] = "AcquireLicense";
const char ckdrm_challenge[] =
    "<?xml version=\"1.0\"><soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\"><soap:Header></soap:Header><soap:Body><AcquireLicense \"\"></AcquireLicense></soap:Body></soap:Envelope>";
const char ckdrm_challenge_response[] =
    "<?xml version=\"1.0\"><soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\"><soap:Header></soap:Header><soap:Body><AcquireLicenseResponse \"granted\"></AcquireLicenseResponse></soap:Body></soap:Envelope>";
const char ckdrm_xml_node_name[] = "ClearKeyContentId";
static gchar clearkey[] = {
  0xe6, 0xdd, 0x42, 0x8e, 0x76, 0xcc, 0x15, 0x44,
  0x41, 0xa2, 0xed, 0x3c, 0xec, 0xe4, 0xa3, 0xdb
};

#define UUID_STRING_LEN 36
#define KEY_SIZE 16

/* pad templates */
static GstStaticPadTemplate
    gst_ckdrm_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-cenc, original-media-type=(string)"
        "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "}, "
        "protection-system=(string) " CKDRM_UUID)
    );

static GstStaticPadTemplate gst_ckdrm_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264; video/x-h265; " SRC_DECODE_AUDIO_CAPS)
    );

/* class initialization */
#define gst_ckdrm_parent_class parent_class
G_DEFINE_TYPE (GstCkDrm, gst_ckdrm, GST_TYPE_BASEDRM);

static void
gst_ckdrm_destroy_key (gpointer data)
{
  KeyMap *key_map = (KeyMap *) data;

  g_free (key_map->key_id);
  g_free (key_map->key);
  g_slice_free (KeyMap, key_map);
}

static gboolean
gst_ckdrm_get_key (GstCkDrm * ckdrm, GBytes * kid_bytes, guint8 type)
{
  gchar *license = ckdrm->jwk;
  gsize kid_size = 0;
  gint i;
  gchar *kid_data;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM get key. key %s", license);

  if (!license) {
    GST_ERROR_OBJECT (ckdrm, "Keys are not set yet");
    return FALSE;
  }

  if (!gst_jwk_extract_key_from_license (license, ckdrm->keys)) {
    GST_ERROR_OBJECT (ckdrm, "Failed to extract key from license");
    return FALSE;
  }

  kid_data = (gchar *) g_bytes_get_data (ckdrm->kid_bytes, &kid_size);

  for (i = 0; i < ckdrm->keys->len; i++) {
    if (!memcmp (((KeyMap *) g_ptr_array_index (ckdrm->keys, i))->key_id,
            kid_data, kid_size)) {
      if (ckdrm->key_info) {
        g_bytes_unref (ckdrm->key_info->key_id);
        g_bytes_unref (ckdrm->key_info->key);
        g_slice_free (GstCkDrmKey, ckdrm->key_info);
      }
      ckdrm->key_info = (GstCkDrmKey *) g_slice_new0 (GstCkDrmKey);
      ckdrm->key_info->key_id =
          g_bytes_new (((KeyMap *) g_ptr_array_index (ckdrm->keys, i))->key_id,
          KEY_SIZE);
      ckdrm->key_info->key =
          g_bytes_new (((KeyMap *) g_ptr_array_index (ckdrm->keys, i))->key,
          KEY_SIZE);
      ckdrm->key_info->type = type;
      ret = TRUE;
      break;
    }
  }

  return ret;
}

static void
gst_ckdrm_class_init (GstCkDrmClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ckdrm_debug_category,
      "ckdrm", 0, "ST ClearKey DRM");

  GST_DEBUG ("ckdrm class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ckdrm_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ckdrm_src_template));

  gst_element_class_set_static_metadata (element_class,
      "ST ClearKey DRM",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts ClearKey DRM protected media in ISOBMFF CENC format",
      "STMicroelectronics");

  basedrm_class->get_drm_info = GST_DEBUG_FUNCPTR (gst_ckdrm_drm_info);
  basedrm_class->start = GST_DEBUG_FUNCPTR (gst_ckdrm_start);
  basedrm_class->stop = GST_DEBUG_FUNCPTR (gst_ckdrm_stop);
  basedrm_class->drm_init = GST_DEBUG_FUNCPTR (gst_ckdrm_drm_init);
  basedrm_class->get_license_challenge =
      GST_DEBUG_FUNCPTR (gst_ckdrm_get_license_challenge);
  basedrm_class->request_license =
      GST_DEBUG_FUNCPTR (gst_ckdrm_request_license);
  basedrm_class->store_license = GST_DEBUG_FUNCPTR (gst_ckdrm_store_license);
  basedrm_class->is_playback_allowed =
      GST_DEBUG_FUNCPTR (gst_ckdrm_is_playback_allowed);
  basedrm_class->prepare_decrypt =
      GST_DEBUG_FUNCPTR (gst_ckdrm_prepare_decrypt);
  basedrm_class->set_kid = GST_DEBUG_FUNCPTR (gst_ckdrm_set_kid);
  basedrm_class->set_iv = GST_DEBUG_FUNCPTR (gst_ckdrm_set_iv);
  basedrm_class->decrypt = GST_DEBUG_FUNCPTR (gst_ckdrm_decrypt);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_ckdrm_set_property);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_ckdrm_change_state);

  g_object_class_install_property (gobject_class, PROP_KEY,
      g_param_spec_string ("key", "Key", "Decryption key", "default",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_JWK,
      g_param_spec_string ("jwk", "jwk", "Json Web Key", "default",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_ckdrm_init (GstCkDrm * ckdrm)
{
  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM initialization");

  ckdrm->drm_handle = NULL;
  ckdrm->license_url = NULL;
  ckdrm->license_url_length = 0;
  ckdrm->type = CKDRM_MAX_TYPE;
  ckdrm->license_state = CKDRM_MAX_LICENSE;
  ckdrm->keys = NULL;
  ckdrm->kid_bytes = NULL;
  ckdrm->key_info = NULL;
  ckdrm->jwk = NULL;
  g_cond_init (&ckdrm->get_key_cond);
  ckdrm->cancelled = FALSE;
  ckdrm->retry_count = 0;
  ckdrm->ex_kid = NULL;
}

static gboolean
gst_ckdrm_drm_info (GstBaseDrm * basedrm, GstDRMSystemInfo * drm_system_info)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM specific static info");

  if (!drm_system_info) {
    GST_ERROR_OBJECT (ckdrm, "NULL pointer drm_system_info");
    return FALSE;
  }

  drm_system_info->system_ids =
      g_list_append (drm_system_info->system_ids, g_strdup (CKDRM_UUID));
  drm_system_info->soap_action =
      g_memdup (&ckdrm_soap_action[0], sizeof (ckdrm_soap_action));
  drm_system_info->xml_node_name =
      g_memdup (&ckdrm_xml_node_name[0], sizeof (ckdrm_xml_node_name));

  if (!drm_system_info->system_ids || !drm_system_info->soap_action
      || !drm_system_info->xml_node_name) {
    GST_ERROR_OBJECT (ckdrm, "Either system_ids %p, soap_action %p or "
        "xml_node_name %p is a NULL pointer", drm_system_info->system_ids,
        drm_system_info->soap_action, drm_system_info->xml_node_name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ckdrm_start (GstBaseDrm * basedrm)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM open");

  ckdrm->keys = g_ptr_array_new_with_free_func (gst_ckdrm_destroy_key);

  if (!gst_aesctr_decrypt_start (&ckdrm->drm_handle)) {
    GST_ERROR_OBJECT (ckdrm, "Failed to open ClearKey DRM");
    goto release;
  }

  return TRUE;

release:
  g_ptr_array_unref (ckdrm->keys);
  ckdrm->keys = NULL;
  return FALSE;
}

static GstStateChangeReturn
gst_ckdrm_change_state (GstElement * element, GstStateChange transition)
{
  GstCkDrm *ckdrm = GST_CKDRM (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM change state");

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (&ckdrm->get_key_mutex);
      ckdrm->cancelled = TRUE;
      g_cond_signal (&ckdrm->get_key_cond);
      g_mutex_unlock (&ckdrm->get_key_mutex);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static gboolean
gst_ckdrm_stop (GstBaseDrm * basedrm)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);
  GstDecryptInfo *decrypt_info = &basedrm->decrypt_info;

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM close");

  if (decrypt_info->scheme_type == FOURCC_cbcs) {
    gst_aescbcs_decrypt_stop ();
  } else {
    gst_aesctr_decrypt_stop (ckdrm->drm_handle);
    ckdrm->drm_handle = NULL;
  }

  if (ckdrm->jwk) {
    g_free (ckdrm->jwk);
    ckdrm->jwk = NULL;
  }

  if (ckdrm->license_url) {
    g_free (ckdrm->license_url);
    ckdrm->license_url = NULL;
  }

  if (ckdrm->keys) {
    g_ptr_array_unref (ckdrm->keys);
    ckdrm->keys = NULL;
  }

  if (ckdrm->kid_bytes) {
    g_bytes_unref (ckdrm->kid_bytes);
  }

  if (ckdrm->key_info) {
    g_bytes_unref (ckdrm->key_info->key_id);
    g_bytes_unref (ckdrm->key_info->key);
    g_slice_free (GstCkDrmKey, ckdrm->key_info);
  }

  g_mutex_clear (&ckdrm->get_key_mutex);
  g_cond_clear (&ckdrm->get_key_cond);

  if (ckdrm->ex_kid) {
    g_free (ckdrm->ex_kid);
    ckdrm->ex_kid = NULL;
  }

  return TRUE;
}

static gboolean
get_key_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  GstCkDrm *ckdrm = (GstCkDrm *) user_data;

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM get key callback");

  g_mutex_lock (&ckdrm->get_key_mutex);
  if (!gst_ckdrm_get_key (ckdrm, ckdrm->kid_bytes, ckdrm->type)) {
    GST_ERROR_OBJECT (ckdrm, "Failed to get a key for %s media type",
        (ckdrm->type == CKDRM_VIDEO_TYPE) ? "VIDEO" : "AUDIO");
    if (ckdrm->retry_count > 5) {
      GST_DEBUG_OBJECT (ckdrm, "Posting waitingforkey message to application");

      gst_element_post_message (GST_ELEMENT_CAST (ckdrm),
          gst_message_new_element (GST_OBJECT_CAST (ckdrm),
              gst_structure_new_empty ("waitingforkey")));

      ckdrm->retry_count = 0;
    }
    ckdrm->retry_count++;
  } else {
    GST_DEBUG_OBJECT (ckdrm, "Success to get a key for %s media type",
        (ckdrm->type == CKDRM_VIDEO_TYPE) ? "VIDEO" : "AUDIO");
    ckdrm->license_state = CKDRM_LICENSE_GRANTED;
    g_cond_signal (&ckdrm->get_key_cond);
  }
  g_mutex_unlock (&ckdrm->get_key_mutex);

  return TRUE;
}

static gboolean
gst_ckdrm_prepare_decrypt (GstBaseDrm * basedrm)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);
  GstCaps *src_caps;
  const gchar *name;

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM prepare decrypt context");

  src_caps =
      gst_pad_get_current_caps (GST_BASE_TRANSFORM_SRC_PAD (&ckdrm->parent));
  if (!src_caps) {
    GST_ERROR_OBJECT (ckdrm, "Source caps unknown");
    return FALSE;
  }
  name = gst_structure_get_name (gst_caps_get_structure ((src_caps), 0));
  gst_caps_unref (src_caps);
  GST_DEBUG_OBJECT (ckdrm, "Src pad structure name %s", name);

  if (strncmp (name, "video/", 6) == 0) {
    ckdrm->type = CKDRM_VIDEO_TYPE;
  } else if (strncmp (name, "audio/", 6) == 0) {
    ckdrm->type = CKDRM_AUDIO_TYPE;
  } else {
    GST_ERROR_OBJECT (ckdrm, "Unsupported media type");
    return FALSE;
  }

  GST_DEBUG_OBJECT (ckdrm, "Media type %s", (ckdrm->type == CKDRM_VIDEO_TYPE) ?
      "VIDEO" : "AUDIO");

  return TRUE;
}

static gboolean
gst_ckdrm_periodic_key (GstBaseDrm * basedrm)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);
  GstClock *clock;
  GstClockID clock_id;
  GstClockTime base;
  GstClockReturn wait_ret;

  clock = gst_system_clock_obtain ();
  if (!clock) {
    GST_ERROR_OBJECT (ckdrm, "Failed to create instance of GstSystemClock");
    return FALSE;
  }
  base = gst_clock_get_time (clock);

  clock_id = gst_clock_new_periodic_id (clock, base, GST_SECOND);
  if (!clock_id) {
    GST_ERROR_OBJECT (ckdrm, "Failed to create periodic id");
    return FALSE;
  }

  g_mutex_lock (&ckdrm->get_key_mutex);
  if (G_UNLIKELY (ckdrm->cancelled)) {
    g_mutex_unlock (&ckdrm->get_key_mutex);
    goto release;
  }

  wait_ret = gst_clock_id_wait_async (clock_id, get_key_callback, ckdrm, NULL);
  if (wait_ret != GST_CLOCK_OK) {
    gst_clock_id_unref (clock_id);
    gst_object_unref (G_OBJECT (clock));
  }

  g_cond_wait (&ckdrm->get_key_cond, &ckdrm->get_key_mutex);
  g_mutex_unlock (&ckdrm->get_key_mutex);

  gst_clock_id_unschedule (clock_id);

release:
  gst_clock_id_unref (clock_id);
  gst_object_unref (G_OBJECT (clock));

  if (ckdrm->license_state != CKDRM_LICENSE_GRANTED) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ckdrm_set_kid (GstBaseDrm * basedrm, guint8 * kid_data, gsize kid_size)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM set kid");

  if (ckdrm->kid_bytes)
    g_bytes_unref (ckdrm->kid_bytes);
  ckdrm->kid_bytes = g_bytes_new (kid_data, kid_size);

  if (!ckdrm->ex_kid)
    ckdrm->ex_kid = (guint8 *) g_malloc0 (KEY_SIZE);

  if (!ckdrm->key_info || memcmp (kid_data, ckdrm->ex_kid, KEY_SIZE)) {
    if (!gst_ckdrm_periodic_key (basedrm)) {
      GST_ERROR_OBJECT (ckdrm, "Failed to get key");
      return FALSE;
    }
    memcpy (ckdrm->ex_kid, kid_data, KEY_SIZE);
  }

  return TRUE;
}

static gboolean
gst_ckdrm_set_iv (GstBaseDrm * basedrm, guint8 * iv_data, gsize iv_size)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);
  GstDecryptInfo *decrypt_info = &basedrm->decrypt_info;
  GBytes *iv_bytes = NULL;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM set iv");

  g_return_val_if_fail (ckdrm->key_info != NULL, FALSE);
  g_return_val_if_fail (iv_data != NULL, FALSE);

  iv_bytes = g_bytes_new (iv_data, iv_size);

  if (decrypt_info->scheme_type == FOURCC_cbcs) {
    if (!gst_aescbcs_decrypt_init (ckdrm->key_info->key, iv_bytes)) {
      GST_ERROR_OBJECT (ckdrm, "Failed to initialize the AES CBCS decrypyion");
      ret = FALSE;
    }
  } else {
    if (!gst_aesctr_decrypt_init (ckdrm->drm_handle, ckdrm->key_info->key,
            iv_bytes)) {
      GST_ERROR_OBJECT (ckdrm, "Failed to initialize the AES CTR decryption");
      ret = FALSE;
    }
  }

  if (iv_bytes) {
    g_bytes_unref (iv_bytes);
  }

  return ret;
}

static gboolean
gst_ckdrm_decrypt (GstBaseDrm * basedrm, GstDecryptInfo * decrypt_info)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);
  guint32 subsample_count = decrypt_info->subsample_count;
  guint32 *subsample_info = decrypt_info->subsample_info;
  guint8 *src = decrypt_info->data;
  guint32 i;

  GST_DEBUG_OBJECT (ckdrm, "decrypt");

  for (i = 0; i < subsample_count * 2; i += 2) {
    guint32 n_bytes_clear = subsample_info[i];
    guint32 n_bytes_encrypted = subsample_info[i + 1];

    src += n_bytes_clear;

    if (n_bytes_encrypted) {
      if (decrypt_info->scheme_type == FOURCC_cbcs) {
        if (!gst_aescbcs_decrypt_ip (src, n_bytes_encrypted,
                decrypt_info->crypt_byte_block,
                decrypt_info->skip_byte_block)) {
          GST_ERROR_OBJECT (ckdrm, "Failed to decrypt with AES CBCS mode");
          return FALSE;
        }
      } else {
        if (!gst_aesctr_decrypt_ip (ckdrm->drm_handle, src, n_bytes_encrypted)) {
          GST_ERROR_OBJECT (ckdrm, "Failed to decrypt with AES CTR mode");
          return FALSE;
        }
      }
      src += n_bytes_encrypted;
    }
  }

  return TRUE;
}

static gboolean
gst_ckdrm_is_playback_allowed (GstBaseDrm * basedrm)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "is_playback_allowed");

  if (ckdrm->license_state != CKDRM_LICENSE_GRANTED) {
    return FALSE;
  }

  return TRUE;

  //return ((ckdrm->license_state == CKDRM_LICENSE_GRANTED) ? TRUE : FALSE);
}

/* ClearKey protection system specific data:
 * license_url_length [1 byte]
 * license_url        [license_url_length bytes]
 * key_count          [4 bytes]
 * {
 *   media_type       [1 byte]
 *   key              [KEY_SIZE bytes]
 * } [key_count]
 */
static gboolean
gst_ckdrm_drm_init (GstBaseDrm * basedrm, guint8 * header, guint size)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "ClearKey DRM initialization");

  /* TODO: Parse PSSH box data here if it needs more information for DRM processing */

  return TRUE;
}

static gboolean
gst_ckdrm_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "get_license_challenge");

  /* TODO: fill up drm_licence_info for license challenge */

  return TRUE;
}

static gboolean
gst_ckdrm_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "request_license");

  /* TODO: request license via http protocol */

  return TRUE;
}

static gboolean
gst_ckdrm_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCkDrm *ckdrm = GST_CKDRM (basedrm);

  GST_DEBUG_OBJECT (ckdrm, "store_license");

  /* TODO: verify license received from license server. */

  return TRUE;
}

static void
gst_ckdrm_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCkDrm *self = GST_CKDRM (object);
  const gchar *str;
  guint num, offset = 0;

  switch (prop_id) {
    case PROP_KEY:
      str = g_value_get_string (value);
      if (str) {
        while (sscanf (str + offset, "%2x", &num) == 1) {
          clearkey[offset / 2] = (gchar) num;
          offset += 2;
        }
      } else {
        GST_ERROR_OBJECT (self, "Key value is NULL");
      }
      break;
    case PROP_JWK:
      self->jwk = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }
}

gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "ckdrm", GST_RANK_PRIMARY,
          gst_ckdrm_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ckdrm,
    "ClearKey DRM",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
