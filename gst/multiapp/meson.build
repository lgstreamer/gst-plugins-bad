multiapp_sources = [
  'multiapp.c',
]

gstmultiappsrc = library('gstmultiappsrc',
  multiapp_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  dependencies : [gstmultiapp_dep, gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
