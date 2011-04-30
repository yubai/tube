#include "http/module.h"

extern void tube_http_static_module_init(void);

static tube_module_t module = {
    .name = "mod_static",
    .vendor = "tube server",
    .description = "Static Handler in Tube",
    .on_initialize = tube_http_static_module_init
};

EXPORT_MODULE_STATIC(module);
