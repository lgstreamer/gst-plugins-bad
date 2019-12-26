tsdemux_sources = [
  'mpegtspacketizer.c',
  'mpegtsbase.c',
  'mpegtsparse.c',
  'tsdemux.c',
  'gsttsdemux.c',
  'pesparse.c',
]

gstmepgtsdemux_libs = []
libdl = cc.find_library('dl', required: false)
if libdl.found()
    gstmpegtsdemux_libs = [libdl]
endif

gstmpegtsdemux = library('gstmpegtsdemux',
  tsdemux_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  dependencies : [gstcodecparsers_dep, gstmpegts_dep, gsttag_dep,
		  gstpbutils_dep, gstaudio_dep, gstbase_dep, mathlib] + gstmpegtsdemux_libs,
  install : true,
  install_dir : plugins_install_dir,
)
