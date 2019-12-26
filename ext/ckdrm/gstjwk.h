/* Json Web Key processing module
 * Copyright (C) 2017 LG Electronics, Inc.
 * Author : Chihyoung Kim <chihyoung2.kim@lge.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __JSON_WEB_KEY_H__
#define __JSON_WEB_KEY_H__

#include <glib.h>

typedef struct _KeyMap
{
  gchar *key_id;
  gchar *key;
} KeyMap;

G_BEGIN_DECLS gboolean gst_jwk_extract_key_from_license (const gchar *json_web_key, GPtrArray *keys);

G_END_DECLS
#endif /* __JSON_WEB_KEY_H__ */
