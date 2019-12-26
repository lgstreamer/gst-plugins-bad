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

#ifndef  __GST_BASEDRM_H__
#define  __GST_BASEDRM_H__

#include <gst/base/gstbasetransform.h>

/* Begin Declaration */
G_BEGIN_DECLS

#ifdef DISABLE_FLAC
#define FLAC_AUDIO_CAPS
#else
#define FLAC_AUDIO_CAPS \
    "audio/x-flac"
#endif

#define SINK_DECODE_AUDIO_CAPS \
    "audio/mpeg, "\
    "audio/mpeg-h, "\
    "audio/x-dts, " \
    "audio/x-dtsh, " \
    "audio/x-dtsl, " \
    "audio/x-dtse, " \
    "audio/x-ac3, " \
    "audio/x-eac3, " \
    "audio/x-ac4, " \
    "audio/x-private1-ac3, " \
    "audio/x-wma, " \
    "audio/x-lpcm-1, " \
    "audio/x-lpcm, " \
    "audio/x-private-lg-lpcm, " \
    "audio/x-private1-lpcm, " \
    "audio/x-private-ts-lpcm, " \
    "audio/x-adpcm, " \
    "audio/x-vorbis, " \
    "audio/AMR, " \
    "audio/AMR-WB, " \
    FLAC_AUDIO_CAPS ", " \
    "audio/x-mulaw, " \
    "audio/x-alaw, " \
    "audio/x-private1-dts, " \
    "audio/x-opus"

#define SRC_DECODE_AUDIO_CAPS \
    "audio/mpeg;"\
    "audio/mpeg-h;"\
    "audio/x-dts;" \
    "audio/x-dtsh;" \
    "audio/x-dtsl;" \
    "audio/x-dtse;" \
    "audio/x-ac3;" \
    "audio/x-eac3;" \
    "audio/x-ac4;" \
    "audio/x-private1-ac3;" \
    "audio/x-wma;" \
    "audio/x-lpcm-1;" \
    "audio/x-lpcm;" \
    "audio/x-private-lg-lpcm;" \
    "audio/x-private1-lpcm;" \
    "audio/x-private-ts-lpcm;" \
    "audio/x-adpcm;" \
    "audio/x-vorbis;" \
    "audio/AMR;" \
    "audio/AMR-WB;" \
    FLAC_AUDIO_CAPS ";" \
    "audio/x-mulaw;" \
    "audio/x-alaw;" \
    "audio/x-private1-dts;" \
    "audio/x-opus"

#define GST_TYPE_BASEDRM                (gst_basedrm_get_type())
#define GST_BASEDRM(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASEDRM,GstBaseDrm))
#define GST_BASEDRM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASEDRM,GstBaseDrmClass))
#define GST_BASEDRM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_BASEDRM, GstBaseDrmClass))
#define GST_IS_BASEDRM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASEDRM))
#define GST_IS_BASEDRM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASEDRM))
#define GST_BASEDRM_CAST(obj)           ((GstBaseDrm *) (obj))

#define FOURCC_cenc    GST_MAKE_FOURCC('c','e','n','c')
#define FOURCC_cbc1    GST_MAKE_FOURCC('c','b','c','1')
#define FOURCC_cens    GST_MAKE_FOURCC('c','e','n','s')
#define FOURCC_cbcs    GST_MAKE_FOURCC('c','b','c','s')
#define AES128_BLOCKSIZE_RADIX2 4

typedef struct _GstBaseDrm GstBaseDrm;
typedef struct _GstBaseDrmClass GstBaseDrmClass;
typedef struct _GstContentProtection GstContentProtection;
typedef struct _GstDRMLicenseInfo GstDRMLicenseInfo;
typedef struct _GstDRMSystemInfo GstDRMSystemInfo;
typedef struct _GstDecryptInfo GstDecryptInfo;
typedef struct _GstDRMRightsErrorInfo GstDRMRightsErrorInfo;

/**
 * GstContentProtection:
 * This struct is used to store DRM protection data extracted from XML element from dash demuxer
 */
struct _GstContentProtection
{
  gchar *schemeIdUri;           /* unique identifier of the DRM system */
  gchar *value;                 /* DRM system and version */
  gchar *KID;                   /* default key identifier */
  gchar *data;                  /* pssh data */
};

/**
 * GstDRMLicenseInfo:
 * This struct is used to share DRM license info with subclass
 */
struct _GstDRMLicenseInfo
{
  /* license specific info
   * subclass uses these variables @get_license_challenge call */
  guint8 *url;
  guint32 url_length;
  guint8 *challenge;
  guint32 challenge_length;

  /* license from server
   * subclass uses these variables in @store_license call */
  guint8 *response;
  guint32 response_length;

  /* license acknowledgement info, reserved for future use */
  guint8 *ack;
  guint32 ack_length;
  guint8 *ack_response;
  guint32 ack_response_length;
};

/**
 * GstDRMSystemInfo:
 * This struct is used to retrieve DRM system specific info from subclass
 */
struct _GstDRMSystemInfo
{
  GList *system_ids;             /* protection system ID */
  guint8 *soap_action;          /* SOAPAction intent uri */
  guint8 *xml_node_name;        /* XML node name */
  gchar *drmclient_id;          /* drmclient id for custom data processing */
  gboolean is_svp;              /* svp flag for decryption */
};

/**
 * GstDecryptInfo:
 * This struct is used to share decrypt info with subclass
 */
struct _GstDecryptInfo
{
  guint8 *data;                 /* Decrypt buffer */
  guint data_size;              /* Decrypt buffer size */
  guint32 scheme_type;          /* Protection scheme type */
  guint  crypt_byte_block;      /* Count of encrypted Blocks */
  guint  skip_byte_block;       /* Count of unencrypted Blocks */
  guint32 *subsample_info;      /* subsample information */
  guint32 subsample_count;      /* count of subsamples */
};

enum DRMRightsErrorState
{
  RIGHTS_ERROR_NONE = -1,
  RIGHTS_ERROR_NO_LICENSE = 0,
  RIGHTS_ERROR_INVALID_LICENSE = 1
};

/**
 * GstDRMRightsErrorInfo
 * This struct is used to raise error information to player
 */
struct _GstDRMRightsErrorInfo
{
  enum DRMRightsErrorState error_state; /* Rights error code */
  gchar *content_id;                    /* The unique ID of protected content */
  gchar *drm_system_id;                 /* DRM sysmtem ID */
  gchar *rights_issuer_url;             /* DRM rights issuer URL */
};

/**
 * GstBaseDrm:
 * Base DRM object
 */
struct _GstBaseDrm
{
  GstBaseTransform parent;

  GstDecryptInfo decrypt_info;  /* Decryptor Info */
  GstDRMSystemInfo drm_system_info;     /* DRM system specific info */
  GstDRMLicenseInfo drm_license_info;   /* DRM License specific info */
  GstDRMRightsErrorInfo rights_error_info; /* DRM Rights error info */

  gchar *proxy_id;
  gchar *proxy_pw;

  /* Padding bytes */
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstBaseDrmClass:
 * @get_drm_info: The subclass provides the DRM system specific info
 *    subclass provides following fields in drm_system_info.
 *    system_ids: protection system ID is a fixed size string to identify the various DRM system
 *    soap_action: The SOAPAction HTTP request header field can be used to indicate the intent
 *                 of the SOAP HTTP request. The value is a URI identifying the intent.
 *    memory for system_ids & soap_action is allocated by subclass and freed by basedrm class
 *
 * @start: Called to start the drm system
 *      subclass open DRM system and does activities like platform initialization
 *
 * @drm_init: Initialize DRM system by setting DRM header object
 *
 * @get_license_challenge: Called to get url and license challenge
 *      Subclass provides following fields of GstDRMLicenseInfo
 *     *url: license server URL, memory for url is allocated by subclass and freed by basedrm class
 *      url_length: url length
 *     *challenge: license challenge string,  memory for challenge is allocated by subclass and freed by basedrm class
 *      challenge_length: challenge length
 *
 * @store_license: To store the license retrieved from server
 *     Subclass retrieves following fields of GstDRMLicenseInfo
 *    *response: license string from license server
 *     response_length: license string length
 *
 * @is_playback_allowed: To check if playback is allowed as per stored license
 *
 * @prepare_decrypt: Call to prepare decrypt context
 *
 * @decrypt: Call to decrypt the data.
 *    All info regarding encrypted data is stored in decrypt_info of type GstDecryptInfo
 *    decrypt_info provides following info to subclass
 *    data              Decrypt buffer
 *    data_size         Decrypt buffer size
 *    offset_block      Block offset within the payload
 *    offset_byte       Byte offset within the block
 *    sample_ID         Decrypt sample ID
 *
 * @set_iv: To set Initialization Vector (IV) required for sample decryption
 *
 * @stop: Called to stop the DRM system
 *        DRM system can remove current license, close DRM and do other cleanups in this call.
 **/

struct _GstBaseDrmClass
{
  GstBaseTransformClass parent_class;

    gboolean (*get_drm_info) (GstBaseDrm * basedrm,
      GstDRMSystemInfo * drm_system_info);

    gboolean (*start) (GstBaseDrm * basedrm);

    gboolean (*drm_init) (GstBaseDrm * basedrm, guint8 * header, guint size);

    gboolean (*get_license_challenge) (GstBaseDrm * basedrm,
      GstDRMLicenseInfo * drm_license_info);

    gboolean (*request_license) (GstBaseDrm * basedrm,
      GstDRMLicenseInfo * drm_license_info);

    gboolean (*store_license) (GstBaseDrm * basedrm,
      GstDRMLicenseInfo * drm_license_info);

    gboolean (*is_playback_allowed) (GstBaseDrm * basedrm);

    gboolean (*prepare_decrypt) (GstBaseDrm * basedrm);

    gboolean (*set_kid) (GstBaseDrm * basedrm, guint8 * kid_data, gsize kid_size);

    gboolean (*set_iv) (GstBaseDrm * basedrm, guint8 * iv_data, gsize iv_size);

    gboolean (*decrypt) (GstBaseDrm * basedrm, GstDecryptInfo * decrypt_info);

    gboolean (*resolve_custom_pssi) (GstBaseDrm * basedrm, gchar * custom_pssi, guint8 ** header, guint * size);

    GstBuffer * (*get_key_info) (GstBaseDrm * basedrm);

    gboolean (*stop) (GstBaseDrm * basedrm);
};

GST_EXPORT
GType gst_basedrm_get_type (void);

G_END_DECLS
#endif /* __GST_BASEDRM_H__ */
