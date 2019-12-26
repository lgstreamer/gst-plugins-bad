badbasedrm_sources = ['gstbasedrm.c']

xml2_dep = dependency('libxml-2.0', version : '>= 2.8', required : false)
libsoup_dep = dependency('libsoup-2.4', version : '>=2.48', required : false)
if xml2_dep.found() and libsoup_dep.found()
  gstbasedrm = library('gstbasedrm-' + api_version,
    badbasedrm_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    install : true,
    dependencies : [gstbase_dep, xml2_dep, libsoup_dep]
  )
endif

gstbasedrm_dep = declare_dependency(link_with : gstbasedrm,
  include_directories : [libsinc],
  dependencies : [gstbase_dep, xml2_dep, libsoup_dep])
