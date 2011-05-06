#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#include "http/module.h"

#define MAX_MODULE_CNT 256
#define MAX_ERROR_LENGTH 1024

static size_t         nr_module;
static tube_module_t* modules[MAX_MODULE_CNT];
static char*          last_error = NULL;

static inline void
set_last_error(const char* error)
{
    free(last_error);
    if (error != NULL) {
        last_error = strndup(error, 1024);
    }
}

const char*
tube_module_last_error()
{
    return last_error;
}

tube_module_t*
tube_module_load(const char* filename)
{
    void* handle = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
    void* sym_handle = NULL;
    tube_module_t* module_ptr = NULL;

    if (handle == NULL) {
        set_last_error(dlerror());
        return NULL;
    }

    sym_handle = dlsym(handle, EXPORT_MODULE_PTR_NAME);
    if (sym_handle == NULL) {
        dlclose(handle);
        set_last_error(dlerror());
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

static inline int
is_file_shared_object(const char* filename)
{
    if (filename == NULL) {
        return 0;
    } else if (filename[0] == 0 || filename[0] == '.') {
        return 0;
    }
    const char* ext = filename + strlen(filename) - 3;
    if (strcmp(ext, ".so") != 0) {
        return 0;
    }
    return 1;
}

int
tube_module_load_dir(const char* dirname)
{
    long max_path_length = pathconf(dirname, _PC_PATH_MAX);
    DIR* dir = NULL;
    char* path = NULL;

    if (max_path_length < 0) {
        return 0;
    }

    path = malloc(max_path_length);
    dir = opendir(dirname);
    if (dir == NULL || path == NULL) {
        return 0;
    }

    struct dirent* ent = NULL;
    while ((ent = readdir(dir))) {
        const char* fname = ent->d_name;
        tube_module_t* module = NULL;

        if (!is_file_shared_object(fname)) {
            continue;
        }

        snprintf(path, max_path_length, "%s/%s", dirname, fname);
        module = tube_module_load(path);
        if (module) {
            tube_module_register_module(module);
        } else {
            fprintf(stderr, "%s\n", tube_module_last_error());
        }
    }
    free(path);
    closedir(dir);
    return 1;
}
