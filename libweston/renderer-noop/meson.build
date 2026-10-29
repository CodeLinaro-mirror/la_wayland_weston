srcs_renderer_noop = [
	'noop-renderer.c',
]

deps_renderer_noop = [
#	dep_libdrm,
	dep_libweston_private,
	dep_pixman,
]

plugin_noop = shared_library(
	'noop-renderer',
	srcs_renderer_noop,
	include_directories: common_inc,
	dependencies: deps_renderer_noop,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
env_modmap += 'noop-renderer.so=@0@;'.format(plugin_noop.full_path())
