#include "http/module.h"

extern void tube_http_python_module_init(void);
extern void tube_http_python_module_finit(void);

static tube_module_t module = {
    .name = "mod_python",
    .vendor = "tube server",
    .description = "Python Handler in Tube",
    .on_initialize = tube_http_python_module_init,
    .on_finalize = tube_http_python_module_finit
};

EXPORT_MODULE(module);
