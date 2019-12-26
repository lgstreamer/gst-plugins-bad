vcd_sources = [
  'vcdsrc.c'
]

gstvcdsrc = library('gstvcdsrc',
  vcd_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
