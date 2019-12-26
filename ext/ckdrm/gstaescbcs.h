/*
* This file is part of ClearKey DRM
*
* Copyright (C) 2018, STMicroelectronics - All Rights Reserved
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

#ifndef  __GST_AESCBCS_H__
#define  __GST_AESCBCS_H__

#include <glib.h>

G_BEGIN_DECLS
gboolean gst_aescbcs_decrypt_init (GBytes * key, GBytes * iv);
void gst_aescbcs_decrypt_stop (void);
gboolean gst_aescbcs_decrypt_ip (unsigned char *data, int length, guint crypt_byte_block, guint skip_byte_block);

G_END_DECLS
#endif /* __GST_AESCBCS_H__ */
