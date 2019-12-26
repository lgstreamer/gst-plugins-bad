/*
* This file is part of ClearKey DRM
*
* Copyright (C) 2015-2016, STMicroelectronics - All Rights Reserved
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

#ifndef  __GST_AESCTR_H__
#define  __GST_AESCTR_H__

#include <glib.h>

G_BEGIN_DECLS gboolean gst_aesctr_decrypt_start (void **drm_handle);
void gst_aesctr_decrypt_stop (void *drm_handle);
gboolean gst_aesctr_decrypt_init (void *drm_handle, GBytes * key, GBytes * iv);
gboolean gst_aesctr_decrypt_ip (void *drm_handle, unsigned char *data,
    int length);

G_END_DECLS
#endif /* __GST_AESCTR_H__ */
