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

#include <string.h>
#include <json.h>
#include <gst/gst.h>

#include "gstjwk.h"

guchar *gst_jwk_decode_base64_string (gchar * encoded_text);
void gst_jwk_parse_json_value (json_object * jobj, gchar * key,
    GPtrArray * tokens);
void gst_jwk_parse_json_array (json_object * jobj, gchar * key,
    GPtrArray * tokens);
void gst_jwk_parse_json_object (json_object * jobj, GPtrArray * tokens);

guchar *
gst_jwk_decode_base64_string (gchar * encoded_text)
{
  gsize size = 0;
  gint remainder = 0, i;
  gchar padded_text[1024] = { 0, };

  // encode_text should not contain padding characters according to EME spec.
  if (g_strrstr (encoded_text, "=")) {
    return NULL;
  }

  g_strlcpy (padded_text, encoded_text, 1024);

  // the characters '-' and '_' MUST be used instead of '+' and '/', respectively.
  // So, it needs to be converted to original string.
  for (i = 0; padded_text[i] != '\0'; i++) {
    if (padded_text[i] == '-') {
      padded_text[i] = '+';
    } else if (padded_text[i] == '_') {
      padded_text[i] = '/';
    }
  }

  remainder = strlen (encoded_text) % 4;
  if (remainder > 0) {
    for (i = 0; i < 4 - remainder; i++) {
      g_strlcat (padded_text, "=", 1024);
    }
  }

  return g_base64_decode (padded_text, &size);
}

void
gst_jwk_parse_json_value (json_object * jobj, gchar * key, GPtrArray * tokens)
{
  enum json_type type;
  const gchar *value;

  type = json_object_get_type (jobj);
  switch (type) {
    case json_type_boolean:
    case json_type_double:
    case json_type_int:
    case json_type_null:
      break;
    case json_type_string:
      value = json_object_get_string (jobj);
      if (!g_strcmp0 (key, "k") || !g_strcmp0 (key, "kid")) {
        g_ptr_array_add (tokens, (gpointer) key);
        g_ptr_array_add (tokens, (gpointer) value);
      }
      break;
    default:
      break;
  }
}

void
gst_jwk_parse_json_array (json_object * jobj, gchar * key, GPtrArray * tokens)
{
  enum json_type type;
  gint array_len, i;
  json_object *jvalue;
  json_object *jarray = jobj;

  if (key) {
    json_object *child = NULL;
    if (json_object_object_get_ex (jarray, key, &child))
      jarray = child;
  }

  array_len = json_object_array_length (jarray);

  for (i = 0; i < array_len; i++) {
    jvalue = json_object_array_get_idx (jarray, i);

    type = json_object_get_type (jvalue);
    if (type == json_type_array) {
      gst_jwk_parse_json_array (jobj, NULL, tokens);
    } else if (type != json_type_object) {
      gst_jwk_parse_json_value (jvalue, key, tokens);
    } else {
      gst_jwk_parse_json_object (jvalue, tokens);
    }
  }
}

void
gst_jwk_parse_json_object (json_object * jobj, GPtrArray * tokens)
{
  enum json_type type;

  json_object_object_foreach (jobj, key, val) {
    type = json_object_get_type (val);

    switch (type) {
      case json_type_boolean:
      case json_type_double:
      case json_type_int:
      case json_type_null:
      case json_type_string:
        gst_jwk_parse_json_value (val, key, tokens);
        break;
      case json_type_array:
        gst_jwk_parse_json_array (jobj, key, tokens);
        break;
      case json_type_object:
      {
        json_object *child = NULL;
        if (key && json_object_object_get_ex (jobj, key, &child))
          jobj = child;
        gst_jwk_parse_json_object (jobj, tokens);
      }
        break;
      default:
        break;
    }
  }
}

gboolean
gst_jwk_extract_key_from_license (const gchar * json_web_key, GPtrArray * keys)
{
  GPtrArray *tokens = NULL;
  gint i, j;
  KeyMap *key_map;
  gchar *decoded_text;
  gboolean ret = TRUE;
  json_object *jobj = json_tokener_parse (json_web_key);

  if (!jobj) {
    GST_ERROR ("Failed to parse json web key");
    ret = FALSE;
    goto release;
  }

  tokens = g_ptr_array_new ();
  gst_jwk_parse_json_object (jobj, tokens);

  for (i = 0; i < tokens->len; i += 4) {
    key_map = (KeyMap *) g_slice_new0 (KeyMap);
    for (j = i; j < i + 4; j++) {
      if (!g_strcmp0 (g_ptr_array_index (tokens, j), "k")) {
        decoded_text =
            (gchar *) gst_jwk_decode_base64_string (g_ptr_array_index (tokens,
                j + 1));
        key_map->key = g_memdup (decoded_text, 16);
        g_free (decoded_text);
      } else if (!g_strcmp0 (g_ptr_array_index (tokens, j), "kid")) {
        decoded_text =
            (gchar *) gst_jwk_decode_base64_string (g_ptr_array_index (tokens,
                j + 1));
        key_map->key_id = g_memdup (decoded_text, 16);
        g_free (decoded_text);
      }
    }

    if (key_map->key_id == NULL || key_map->key == NULL) {
      GST_ERROR ("invalid key map!");
      ret = FALSE;
      g_slice_free (KeyMap, key_map);
      goto release;
    }

    g_ptr_array_add (keys, key_map);
  }

release:
  if (tokens) {
    g_ptr_array_free (tokens, TRUE);
  }
  if (jobj) {
    json_object_put (jobj);
  }
  return ret;
}
