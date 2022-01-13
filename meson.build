project('gstmotiondetector', 'cpp',
  version : '1.19.3.1',
  meson_version : '>= 0.59',
  default_options : ['buildtype=debugoptimized']
)

gst_req = '>= 1.19.3'
deps = [
  dependency('glib-2.0'),

  dependency('gstreamer-1.0', version : gst_req),

  dependency('gstreamer-base-1.0', version : gst_req),

  dependency('gstreamer-video-1.0', version : gst_req),

  dependency('opencv4', version : ['>= 3.0.0', '<= 4.6.0'], static: true),
]

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

gstmotiondetector = library('gstmotiondetector',
  'src/gstmotiondetector.cpp',
  cpp_args : ['-DVERSION="1.0"'],
  link_args : '-lgstopencv-1.0',
  dependencies : deps,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstmotiondetector, install_dir : plugins_pkgconfig_install_dir)
plugins = [gstmotiondetector]
