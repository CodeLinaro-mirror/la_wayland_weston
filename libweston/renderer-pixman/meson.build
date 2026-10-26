srcs_renderer_pixman = [
	'pixman-renderer.c',
]

deps_renderer_pixman = [
	dep_libdrm,
	dep_libweston_private,
	dep_pixman,
]

plugin_pixman = shared_library(
	'pixman-renderer',
	srcs_renderer_pixman,
	include_directories: common_inc,
	dependencies: deps_renderer_pixman,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
env_modmap += 'pixman-renderer.so=@0@;'.format(plugin_pixman.full_path())
