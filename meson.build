project('gmplay', 'c',
  version : '2.0.0',
  license : 'MIT',
  default_options : ['c_std=gnu11', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

# Static FFmpeg + FBInk built by scripts/build-deps.sh into deps/<platform>/.
platform = meson.get_external_property('kindle_platform', 'kindlehf')
depdir = meson.current_source_dir() / 'deps' / platform
if not import('fs').exists(depdir / 'lib' / 'libavcodec.a')
  error('Dependencies missing - run: scripts/build-deps.sh ' + platform)
endif

cc = meson.get_compiler('c')
# Link order matters for static libs: dependents before their dependencies.
libdeps = []
foreach l : ['avformat', 'avcodec', 'swscale', 'swresample', 'avutil', 'fbink']
  libdeps += cc.find_library(l, dirs : depdir / 'lib', static : true)
endforeach
libdeps += [cc.find_library('z'), cc.find_library('m'), dependency('threads')]

executable('gmplay',
  'src/main.c', 'src/player.c', 'src/browser.c', 'src/screen.c', 'src/input.c',
  'src/ui.c', 'src/sys.c', 'src/audio.c', 'src/scale.c', 'src/font_data.c',
  include_directories : include_directories('deps' / platform / 'include'),
  dependencies : libdeps,
  install : true)
