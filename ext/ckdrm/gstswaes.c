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

#include <string.h>
#include <gst/gst.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include "gstaesctr.h"
#include "gstaescbcs.h"

#define AES128_BLOCK_SIZE 16

typedef struct _GstAesCtr
{
  AES_KEY key;
  unsigned char ivec[AES_BLOCK_SIZE];
  unsigned char ecount[AES_BLOCK_SIZE];
  unsigned int num;
} GstAesCtr;

EVP_CIPHER_CTX *ctx;

gboolean
gst_aesctr_decrypt_start (void **drm_handle)
{
  GstAesCtr *aes = NULL;

  aes = (GstAesCtr *) g_slice_new0 (GstAesCtr);
  if (!aes) {
    GST_ERROR ("Failed to allocate AES CTR context");
    return FALSE;
  }

  *drm_handle = aes;
  return TRUE;
}

void
gst_aesctr_decrypt_stop (void *drm_handle)
{
  GstAesCtr *aes = (GstAesCtr *) drm_handle;

  if (aes) {
    g_slice_free (GstAesCtr, aes);
  }

  return;
}

gboolean
gst_aesctr_decrypt_init (void *drm_handle, GBytes * key, GBytes * iv)
{
  GstAesCtr *aes = (GstAesCtr *) drm_handle;
  const unsigned char *data;
  gsize size;

  g_return_val_if_fail (aes != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (iv != NULL, FALSE);

  data = (const unsigned char *) g_bytes_get_data (key, &size);
  g_return_val_if_fail (size == AES_BLOCK_SIZE, FALSE);

  if (AES_set_encrypt_key (data, 8 * size, &aes->key)) {
    GST_ERROR ("Failed to set the AES key");
    return FALSE;
  }

  aes->num = 0;
  memset (aes->ecount, 0, AES_BLOCK_SIZE);

  memset (aes->ivec, 0, AES_BLOCK_SIZE);
  data = (const unsigned char *) g_bytes_get_data (iv, &size);
  g_return_val_if_fail (size <= AES_BLOCK_SIZE, FALSE);
  memcpy (aes->ivec, data, size);

  return TRUE;
}

gboolean
gst_aesctr_decrypt_ip (void *drm_handle, unsigned char *data, int length)
{
  GstAesCtr *aes = (GstAesCtr *) drm_handle;

  g_return_val_if_fail (aes != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (length != 0, FALSE);

  AES_ctr128_encrypt (data, data, length, &aes->key, aes->ivec,
      aes->ecount, &aes->num);

  return TRUE;
}

gboolean
gst_aescbcs_decrypt_init (GBytes * key, GBytes * iv)
{
  const unsigned char *key_data;
  const unsigned char *iv_data;
  gsize size;

  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (iv != NULL, FALSE);

  if (!(ctx = EVP_CIPHER_CTX_new ())) {
    GST_ERROR ("failed to create and initialise context");
  }

  key_data = (const unsigned char *) g_bytes_get_data (key, &size);
  iv_data = (const unsigned char *) g_bytes_get_data (iv, &size);

  if (1 != EVP_DecryptInit_ex (ctx, EVP_aes_128_cbc (), NULL, key_data,
          iv_data))
    return FALSE;

  EVP_CIPHER_CTX_set_padding (ctx, 0);

  return TRUE;
}

gboolean
gst_aescbcs_decrypt_ip (unsigned char *data, int length, guint crypt_byte_block,
    guint skip_byte_block)
{
  int len, flen = 0;
  unsigned char *encrypted_data = data;
  guint encrypted_data_size = length;
  guint crypt_byte_size = crypt_byte_block * AES_BLOCK_SIZE;
  guint skip_byte_size = skip_byte_block * AES_BLOCK_SIZE;

  while (encrypted_data_size > 0) {
    if (encrypted_data_size > crypt_byte_size) {
      if (1 != EVP_DecryptUpdate (ctx, encrypted_data, &len, encrypted_data,
              AES_BLOCK_SIZE)) {
        GST_ERROR ("failed to decrypt");
        return FALSE;
      }
      if (1 != EVP_DecryptFinal_ex (ctx, encrypted_data + len, &flen)) {
        GST_ERROR ("failed to finalize decryption");
        return FALSE;
      }
    } else {
      break;
    }

    encrypted_data += crypt_byte_size;
    encrypted_data_size -= crypt_byte_size;

    encrypted_data += MIN (encrypted_data_size, skip_byte_size);
    encrypted_data_size -= MIN (encrypted_data_size, skip_byte_size);
  }

  return TRUE;
}

void
gst_aescbcs_decrypt_stop (void)
{
  EVP_CIPHER_CTX_free (ctx);
}
