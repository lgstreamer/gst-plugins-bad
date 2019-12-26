plugin_LTLIBRARIES = libgstmultiappsrc.la

# sources used to compile this plug-in
libgstmultiappsrc_la_SOURCES = multiapp.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstmultiappsrc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS)
libgstmultiappsrc_la_LIBADD =  $(top_builddir)/gst-libs/gst/multiapp/libgstmultiapp-@GST_API_VERSION@.la $(GST_LIBS)
libgstmultiappsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmultiappsrc_la_LIBTOOLFLAGS = --tag=disable-static
