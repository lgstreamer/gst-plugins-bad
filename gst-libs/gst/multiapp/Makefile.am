
lib_LTLIBRARIES = libgstmultiapp-@GST_API_VERSION@.la

# include $(top_srcdir)/common/gst-glib-gen.mak
CLEANFILES = $(BUILT_SOURCES)

libgstmultiapp_@GST_API_VERSION@_la_SOURCES = gstmultiappsrc.c
libgstmultiapp_@GST_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS)
libgstmultiapp_@GST_API_VERSION@_la_LIBADD = -lgstapp-@GST_API_VERSION@
libgstmultiapp_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstmultiapp_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/multiapp
libgstmultiapp_@GST_API_VERSION@include_HEADERS = \
	gstmultiappsrc.h
