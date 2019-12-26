plugin_LTLIBRARIES = libgstckdrm.la

# sources used to compile this plugin
libgstckdrm_la_SOURCES = \
  gstckdrm.c \
  gstswaes.c \
  gstjwk.c

libgstckdrm_la_CFLAGS = \
 -I$(top_srcdir)/common \
 -I$(top_srcdir)/gst-libs \
  $(GST_CFLAGS) \
  $(GST_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_GST_INCLUDE) \
  $(CKDRM_CFLAGS) \
  $(JSONC_CFLAGS)

libgstckdrm_la_LDFLAGS = \
  $(GST_PLUGIN_LDFLAGS)

libgstckdrm_la_LIBADD = \
  $(GST_PLUGINS_BASE_LIBS) \
  $(GST_BASE_LIBS) \
  $(GST_LIBS) \
  $(CKDRM_LIBS) \
  $(top_builddir)/gst-libs/gst/basedrm/libgstbasedrm-@GST_API_VERSION@.la \
  $(JSONC_LIBS)

libgstckdrm_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
  gstckdrm.h
