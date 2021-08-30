/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

// #include "../posix/main.c"

#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"

static int app_argc;
static char **app_argv;

static int
print_help()
{
    printf("Usage: iwasm [-options] wasm_file [args...]\n");
    printf("options:\n");
    printf("  --stack-size=n         Set maximum stack size in bytes, default is 16 KB\n");
    printf("  --heap-size=n          Set maximum heap size in bytes, default is 16 KB\n");
    printf("  --env=<env>            Pass wasi environment variables with \"key=value\"\n");
    printf("                         to the program, for example:\n");
    printf("                           --env=\"key1=value1\" --env=\"key2=value2\"\n");
    printf("  --dir=<dir>            Grant wasi access to the given host directories\n");
    printf("                         to the program, for example:\n");
    printf("                           --dir=<dir1> --dir=<dir2>\n");
    return 1;
}

static void *
app_instance_main(wasm_module_inst_t module_inst)
{
    const char *exception;

    wasm_application_execute_main(module_inst, app_argc, app_argv);
    if ((exception = wasm_runtime_get_exception(module_inst)))
        printf("%s\n", exception);
    return NULL;
}

static bool
validate_env_str(char *env)
{
    char *p = env;
    int key_len = 0;

    while (*p != '\0' && *p != '=') {
        key_len++;
        p++;
    }

    if (*p != '=' || key_len == 0)
        return false;

    return true;
}

// #if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
#ifdef __NuttX__
static char global_heap_buf[WASM_GLOBAL_HEAP_SIZE * BH_KB] = { 0 };
#else
static char global_heap_buf[10 * 1024 * 1024] = { 0 };
#endif
// #endif

int
main(int argc, char *argv[])
{
    printf("STOP! WAMR TIME\n");

    char *wasm_file = NULL;
    uint8 *wasm_file_buf = NULL;
    uint32 wasm_file_size;
    uint32 stack_size = 16 * 1024, heap_size = 16 * 1024;
    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;
    RuntimeInitArgs init_args;
    char error_buf[128] = { 0 };
    const char *dir_list[8] = { NULL };
    uint32 dir_list_size = 0;
    const char *env_list[8] = { NULL };
    uint32 env_list_size = 0;

    /* Process options.  */
    for (argc--, argv++; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
        if (!strncmp(argv[0], "--stack-size=", 13)) {
            if (argv[0][13] == '\0')
                return print_help();
            stack_size = atoi(argv[0] + 13);
        }
        else if (!strncmp(argv[0], "--heap-size=", 12)) {
            if (argv[0][12] == '\0')
                return print_help();
            heap_size = atoi(argv[0] + 12);
        }
        else if (!strncmp(argv[0], "--dir=", 6)) {
            if (argv[0][6] == '\0')
                return print_help();
            if (dir_list_size >= sizeof(dir_list) / sizeof(char *)) {
                printf("Only allow max dir number %d\n",
                       (int)(sizeof(dir_list) / sizeof(char *)));
                return -1;
            }
            dir_list[dir_list_size++] = argv[0] + 6;
        }
        else if (!strncmp(argv[0], "--env=", 6)) {
            char *tmp_env;

            if (argv[0][6] == '\0')
                return print_help();
            if (env_list_size >= sizeof(env_list) / sizeof(char *)) {
                printf("Only allow max env number %d\n",
                       (int)(sizeof(env_list) / sizeof(char *)));
                return -1;
            }
            tmp_env = argv[0] + 6;
            if (validate_env_str(tmp_env))
                env_list[env_list_size++] = tmp_env;
            else {
                printf("Wasm parse env string failed: expect \"key=value\", "
                       "got \"%s\"\n",
                       tmp_env);
                return print_help();
            }
        }
        else
            return print_help();
    }

    if (argc == 0)
        return print_help();

    wasm_file = argv[0];
    app_argc = argc;
    app_argv = argv;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

    /* initialize runtime environment */
    if (!wasm_runtime_full_init(&init_args)) {
        printf("Init runtime environment failed.\n");
        return -1;
    }

    /* load WASM byte buffer from WASM bin file */
    if (!(wasm_file_buf =
            (uint8 *)bh_read_file_to_buffer(wasm_file, &wasm_file_size)))
        goto fail1;

    /* load WASM module */
    if (!(wasm_module = wasm_runtime_load(wasm_file_buf, wasm_file_size,
                                          error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        goto fail2;
    }

    wasm_runtime_set_wasi_args(wasm_module, dir_list, dir_list_size, NULL, 0,
                               env_list, env_list_size, argv, argc);

    /* instantiate the module */
    if (!(wasm_module_inst =
            wasm_runtime_instantiate(wasm_module, stack_size, heap_size,
                                     error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        goto fail3;
    }

    app_instance_main(wasm_module_inst);

    /* destroy the module instance */
    wasm_runtime_deinstantiate(wasm_module_inst);

fail3:
    /* unload the module */
    wasm_runtime_unload(wasm_module);

fail2:
    wasm_runtime_free(wasm_file_buf);

fail1:
    /* destroy runtime environment */
    wasm_runtime_destroy();
    return 0;
}

