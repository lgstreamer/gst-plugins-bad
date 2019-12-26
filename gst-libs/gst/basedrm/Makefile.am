lib_LTLIBRARIES = libgstbasedrm-@GST_API_VERSION@.la

libgstbasedrm_@GST_API_VERSION@_la_SOURCES = \
  gstbasedrm.c

libgstbasedrm_@GST_API_VERSION@includedir = \
  $(includedir)/gstreamer-@GST_API_VERSION@/gst/basedrm

noinst_HEADERS =

libgstbasedrm_@GST_API_VERSION@include_HEADERS = \
  gstbasedrm.h

libgstbasedrm_@GST_API_VERSION@_la_CFLAGS = \
  $(GST_CFLAGS) \
  $(GST_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_GST_INCLUDE) \
  $(LIBXML2_CFLAGS) \
  $(SOUP_CFLAGS)

libgstbasedrm_@GST_API_VERSION@_la_LIBADD = \
  $(GST_BASE_LIBS) \
  $(GST_LIBS) \
  $(LIBXML2_LIBS) \
  $(SOUP_LIBS)

libgstbasedrm_@GST_API_VERSION@_la_LDFLAGS = \
  $(GST_LIB_LDFLAGS) \
  $(GST_ALL_LDFLAGS) \
  $(GST_LT_LDFLAGS)

libgstbasedrm_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
