badmultiapp_sources = ['gstmultiappsrc.c']
badmultiapp_headers = ['gstmultiappsrc.h']

install_headers(badmultiapp_headers, subdir : 'gstreamer-1.0/gst/multiapp')

gstmultiapp = library('gstmultiapp-' + api_version,
  badmultiapp_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstapp_dep]
)

gstmultiapp_dep = declare_dependency(link_with : gstmultiapp,
  include_directories : [libsinc],
  dependencies : [gstapp_dep])
