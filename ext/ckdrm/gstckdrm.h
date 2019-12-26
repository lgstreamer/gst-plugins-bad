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

#ifndef  __GST_CKDRM_H__
#define  __GST_CKDRM_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

/* Begin Declaration */
G_BEGIN_DECLS
#define GST_TYPE_CKDRM                (gst_ckdrm_get_type())
#define GST_CKDRM(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CKDRM,GstCkDrm))
#define GST_CKDRM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CKDRM,GstCkDrmClass))
#define GST_CKDRM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CKDRM, GstCkDrmClass))
#define GST_IS_CKDRM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CKDRM))
#define GST_IS_CKDRM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CKDRM))
#define GST_CKDRM_CAST(obj)           ((GstCkDrm *) (obj))
typedef struct _GstCkDrm GstCkDrm;
typedef struct _GstCkDrmClass GstCkDrmClass;

enum GstCkDrmType
{
  CKDRM_VIDEO_TYPE = 0,
  CKDRM_AUDIO_TYPE = 1,
  CKDRM_MAX_TYPE = 2
};

typedef struct _GstCkDrmKey
{
  GBytes *key_id;
  GBytes *key;
  enum GstCkDrmType type;
} GstCkDrmKey;

enum GstCkDrmLicense
{
  CKDRM_LICENSE_GRANTED = 0,
  CKDRM_LICENSE_REFUSED = 1,
  CKDRM_MAX_LICENSE = 2
};

/**
 * _GstCkDrm:  ClearKey DRM
 * @parent: Element parent
 */
struct _GstCkDrm
{
  GstBaseDrm parent;

  /* ClearKey specific */
  void *drm_handle;                   /* ClearKey DRM library handle */
  gchar *license_url;                 /* URL for the license challenge */
  guint8 license_url_length;          /* length of the license challenge URL */
  enum GstCkDrmType type;             /* type of the encrypted data */
  enum GstCkDrmLicense license_state; /* license state */
  GPtrArray *keys;                    /* array of GstCkDrmKey objects */
  GBytes *kid_bytes;                  /* key identifier */
  GstCkDrmKey *key_info;              /* key information */
  gchar *jwk;                         /* Json Web Key */
  GCond get_key_cond;                 /* get key cond */
  GMutex get_key_mutex;               /* get key mutex */
  gboolean cancelled;
  guint8 retry_count;
  guint8 *ex_kid;
};

struct _GstCkDrmClass
{
  GstBaseDrmClass parent_class;
};

GType gst_ckdrm_get_type (void);

G_END_DECLS
#endif /* __GST_CKDRM_H__ */
