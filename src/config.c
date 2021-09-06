#include "effect.h"
#include "session.h"
#include "util.h"
#include "window.h"
#include <basedir.h>
#include <confuse.h>
#include <stdio.h>
#include <string.h>

static int validate_unsigned_int(cfg_t *cfg, cfg_opt_t *opt) {
    int value = cfg_opt_getnint(opt, cfg_opt_size(opt) - 1);
    if (value < 0) {
        cfg_error(cfg, "integer option '%s' must be positive in section '%s %s'",
                  opt->name, cfg->name, cfg_title(cfg));
        return -1;
    }
    return 0;
}

static int validate_unsigned_float(cfg_t *cfg, cfg_opt_t *opt) {
    double value = cfg_opt_getnfloat(opt, cfg_opt_size(opt) - 1);
    if (value < 0) {
        cfg_error(cfg, "float option '%s' must be positive in section '%s %s'",
                  opt->name, cfg->name, cfg_title(cfg));
        return -1;
    }
    return 0;
}

static int validate_effect_function(cfg_t *cfg, cfg_opt_t *opt) {
    const char *value = cfg_opt_getnstr(opt, cfg_opt_size(opt) - 1);
    if (!get_effect_func_from_name(value)) {
        cfg_error(cfg, "option '%s' with value '%s' in section '%s %s' is not a supported effect function",
                  opt->name, value, cfg->name, cfg_title(cfg));
        return -1;
    }
    return 0;
}

Bool file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return False;

    fclose(f);
    return True;
}

static const char *config_get_path(const char *config_path) {
    if (file_exists(config_path))
        return config_path;

    const char *default_file[] = {"/compix.conf", "/.compix.conf"};
    char *path = malloc(256);

    const char *config_home = xdgConfigHome(NULL);
    strcpy(path, config_home);
    strcat(path, default_file[0]);
    if (file_exists(path)) {
        free((void *) config_home);
        return path;
    }
    free((void *) config_home);

    const char *home = getenv("HOME");
    if (home) {
        strcpy(path, home);
        strcat(path, default_file[1]);
        if (file_exists(path))
            return path;
    }

    const char *const *config_directories = xdgConfigDirectories(NULL);
    for (int i = 0; config_directories[i]; i++) {
        strcpy(path, config_directories[i]);
        strcat(path, default_file[0]);
        if (file_exists(path)) {
            free((void *) config_directories);
            return path;
        }
    }
    free((void *) config_directories);

    return NULL;
}

void config_get(const char *config_path) {
    const char *path = config_get_path(config_path);

    cfg_opt_t effect_opts[] = {
        CFG_STR("function", NULL, CFGF_NONE),
        CFG_FLOAT("step", 0.03, CFGF_NONE),
        CFG_END()};
    cfg_opt_t wintype_opts[] = {
        CFG_STR("map-effect", NULL, CFGF_NONE),
        CFG_STR("unmap-effect", NULL, CFGF_NONE),
        CFG_STR("create-effect", NULL, CFGF_NONE),
        CFG_STR("destroy-effect", NULL, CFGF_NONE),
        CFG_STR("maximize-effect", NULL, CFGF_NONE),
        CFG_STR("move-effect", NULL, CFGF_NONE),
        CFG_STR("desktop-change-effect", NULL, CFGF_NONE),
        CFG_END()};
    cfg_opt_t effect_rules_opts[] = {
        CFG_SEC("wintype", wintype_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_END()};
    cfg_opt_t opts[] = {
        CFG_INT("effect-delta", 10, CFGF_NONE),
        CFG_SEC("effect", effect_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("effect-rules", effect_rules_opts, CFGF_NONE),
        CFG_END()};
    cfg_t *cfg, *cfg_sec;

    cfg = cfg_init(opts, CFGF_NONE);

    cfg_set_validate_func(cfg, "effect-delta", validate_unsigned_int);
    cfg_set_validate_func(cfg, "effect|step", validate_unsigned_float);
    cfg_set_validate_func(cfg, "effect|function", validate_effect_function);

    if (cfg_parse(cfg, path) == CFG_PARSE_ERROR)
        exit(EXIT_FAILURE);

    s.effect_delta = cfg_getint(cfg, "effect-delta");

    for (int i = 0; i < cfg_size(cfg, "effect"); i++) {
        cfg_sec = cfg_getnsec(cfg, "effect", i);

        char *effect_function = cfg_getstr(cfg_sec, "function");
        if (!effect_function) // TODO put conf file path in error msg
            eprintf("(TODO conf file path here): option 'function' must be set in section 'effect %s'\n", cfg_title(cfg_sec));

        effect_new(cfg_title(cfg_sec), effect_function, cfg_getfloat(cfg_sec, "step"));

        free(effect_function);
    }

    for (int i = 0; i < cfg_size(cfg, "effect-rules|wintype"); i++) {
        cfg_sec = cfg_getnsec(cfg, "effect-rules|wintype", i);

        const char *wintype_name = cfg_title(cfg_sec);
        wintype window_type = get_wintype_from_name(wintype_name);
        if (window_type == WINTYPE_UNKNOWN) // TODO put conf file path in error msg
            eprintf("(TODO conf file path here): wrong wintype '%s' in section 'effect-rules'\n", wintype_name);

        for (int j = 0; j < NUM_EVENT_EFFECTS; j++) {
            char *effect_name = cfg_getstr(cfg_sec, get_event_effect_name(j));
            if (!effect_name)
                continue;
            effect *e = effect_find(effect_name);
            if (!e) // TODO put conf file path in error msg
                eprintf("(TODO conf file path here): effect '%s' in section 'wintype %s' is not defined\n",
                        effect_name, wintype_name);
            effect_set(window_type, j, e);
            free(effect_name);
        }
        free((void *) wintype_name);
    }
}
