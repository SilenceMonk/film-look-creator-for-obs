#include <obs-module.h>
#include "plugin-support.h"
#include "film-look-filter.h" // 包含我们的头文件

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_register_source(&film_look_filter); // 注册滤镜
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
