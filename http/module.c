#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include "http/module.h"

#define MAX_MODULE_CNT 256

static size_t         nr_module;
static tube_module_t* modules[MAX_MODULE_CNT];

tube_module_t*
tube_module_load(const char* filename)
{
    void* handle = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
    void* sym_handle = NULL;
    tube_module_t* module_ptr = NULL;

    if (handle == NULL) {
        return NULL;
    }

    sym_handle = dlsym(handle, EXPORT_MODULE_PTR_NAME);
    if (sym_handle == NULL) {
        dlclose(handle);
        return NULL;
    }
    module_ptr = *(tube_module_t**) sym_handle;
    module_ptr->handle = handle;
    if (module_ptr->on_load) {
        module_ptr->on_load();
    }
    return module_ptr;
}

void
tube_module_register_module(tube_module_t* module)
{
    modules[nr_module] = module;
    nr_module++;
}

void
tube_module_initialize_all()
{
    size_t i;
    printf("initializing modules: ");
    for (i = 0; i < nr_module; i++) {
        printf("%s ", modules[i]->name);
        if (modules[i]->on_initialize) {
            modules[i]->on_initialize();
        }
    }
    puts("");
}

void
tube_module_finalize_all()
{
    size_t i;
    for (i = 0; i < nr_module; i++) {
        if (modules[i]->on_finalize) {
            modules[i]->on_finalize();
        }
        dlclose(modules[i]->handle);
    }
}

#define MAX_PATH_LEN 1024

int
tube_module_load_dir(const char* dirname)
{
    DIR* dir = opendir(dirname);
    if (dir == NULL) {
        return 0;
    }

    char path[MAX_PATH_LEN];
    struct dirent* ent = NULL;
    while ((ent = readdir(dir))) {
        const char* fname = ent->d_name;
        tube_module_t* module = NULL;
        if (fname[0] == '.') continue;
        snprintf(path, MAX_PATH_LEN, "%s/%s", dirname, fname);
        module = tube_module_load(path);
        if (module) {
            tube_module_register_module(module);
        }
    }
    closedir(dir);
    return 1;
}
