
/*
 * Copyright (C) Valentin V. Bartenev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_main.h>
#include <nxt_conf.h>
#include <nxt_router.h>


typedef enum {
    NXT_CONF_VLDT_NULL    = 1 << NXT_CONF_NULL,
    NXT_CONF_VLDT_BOOLEAN = 1 << NXT_CONF_BOOLEAN,
    NXT_CONF_VLDT_INTEGER = 1 << NXT_CONF_INTEGER,
    NXT_CONF_VLDT_NUMBER  = 1 << NXT_CONF_NUMBER,
    NXT_CONF_VLDT_STRING  = 1 << NXT_CONF_STRING,
    NXT_CONF_VLDT_ARRAY   = 1 << NXT_CONF_ARRAY,
    NXT_CONF_VLDT_OBJECT  = 1 << NXT_CONF_OBJECT,
} nxt_conf_vldt_type_t;


typedef struct {
    nxt_str_t             name;
    nxt_conf_vldt_type_t  type;
    nxt_int_t             (*validator)(nxt_conf_validation_t *vldt,
                                       nxt_conf_value_t *value, void *data);
    void                  *data;
} nxt_conf_vldt_object_t;


#define NXT_CONF_VLDT_NEXT(f)  { nxt_null_string, 0, NULL, (f) }
#define NXT_CONF_VLDT_END      { nxt_null_string, 0, NULL, NULL }


typedef nxt_int_t (*nxt_conf_vldt_member_t)(nxt_conf_validation_t *vldt,
                                            nxt_str_t *name,
                                            nxt_conf_value_t *value);

typedef nxt_int_t (*nxt_conf_vldt_system_t)(nxt_conf_validation_t *vldt,
                                            char *name);


static nxt_int_t nxt_conf_vldt_type(nxt_conf_validation_t *vldt,
    nxt_str_t *name, nxt_conf_value_t *value, nxt_conf_vldt_type_t type);
static nxt_int_t nxt_conf_vldt_error(nxt_conf_validation_t *vldt,
    const char *fmt, ...);

static nxt_int_t nxt_conf_vldt_listener(nxt_conf_validation_t *vldt,
    nxt_str_t *name, nxt_conf_value_t *value);
static nxt_int_t nxt_conf_vldt_app_name(nxt_conf_validation_t *vldt,
    nxt_conf_value_t *value, void *data);
static nxt_int_t nxt_conf_vldt_app(nxt_conf_validation_t *vldt,
    nxt_str_t *name, nxt_conf_value_t *value);
static nxt_int_t nxt_conf_vldt_object(nxt_conf_validation_t *vldt,
    nxt_conf_value_t *value, void *data);
static nxt_int_t nxt_conf_vldt_processes(nxt_conf_validation_t *vldt,
    nxt_conf_value_t *value, void *data);
static nxt_int_t nxt_conf_vldt_object_iterator(nxt_conf_validation_t *vldt,
    nxt_conf_value_t *value, void *data);
static nxt_int_t nxt_conf_vldt_system(nxt_conf_validation_t *vldt,
    nxt_conf_value_t *value, void *data);
static nxt_int_t nxt_conf_vldt_user(nxt_conf_validation_t *vldt, char *name);
static nxt_int_t nxt_conf_vldt_group(nxt_conf_validation_t *vldt, char *name);


static nxt_conf_vldt_object_t  nxt_conf_vldt_root_members[] = {
    { nxt_string("listeners"),
      NXT_CONF_VLDT_OBJECT,
      &nxt_conf_vldt_object_iterator,
      (void *) &nxt_conf_vldt_listener },

    { nxt_string("applications"),
      NXT_CONF_VLDT_OBJECT,
      &nxt_conf_vldt_object_iterator,
      (void *) &nxt_conf_vldt_app },

    NXT_CONF_VLDT_END
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_listener_members[] = {
    { nxt_string("application"),
      NXT_CONF_VLDT_STRING,
      &nxt_conf_vldt_app_name,
      NULL },

    NXT_CONF_VLDT_END
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_app_limits_members[] = {
    { nxt_string("timeout"),
      NXT_CONF_VLDT_INTEGER,
      NULL,
      NULL },

    { nxt_string("reschedule_timeout"),
      NXT_CONF_VLDT_INTEGER,
      NULL,
      NULL },

    { nxt_string("requests"),
      NXT_CONF_VLDT_INTEGER,
      NULL,
      NULL },

    NXT_CONF_VLDT_END
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_app_processes_members[] = {
    { nxt_string("spare"),
      NXT_CONF_VLDT_INTEGER,
      NULL,
      NULL },

    { nxt_string("max"),
      NXT_CONF_VLDT_INTEGER,
      NULL,
      NULL },

    { nxt_string("idle_timeout"),
      NXT_CONF_VLDT_INTEGER,
      NULL,
      NULL },

    NXT_CONF_VLDT_END
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_common_members[] = {
    { nxt_string("type"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    { nxt_string("limits"),
      NXT_CONF_VLDT_OBJECT,
      &nxt_conf_vldt_object,
      (void *) &nxt_conf_vldt_app_limits_members },

    { nxt_string("processes"),
      NXT_CONF_VLDT_INTEGER | NXT_CONF_VLDT_OBJECT,
      &nxt_conf_vldt_processes,
      (void *) &nxt_conf_vldt_app_processes_members },

    { nxt_string("user"),
      NXT_CONF_VLDT_STRING,
      nxt_conf_vldt_system,
      (void *) &nxt_conf_vldt_user },

    { nxt_string("group"),
      NXT_CONF_VLDT_STRING,
      nxt_conf_vldt_system,
      (void *) &nxt_conf_vldt_group },

    { nxt_string("working_directory"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    NXT_CONF_VLDT_END
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_python_members[] = {
    { nxt_string("home"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    { nxt_string("path"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    { nxt_string("module"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    NXT_CONF_VLDT_NEXT(&nxt_conf_vldt_common_members)
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_php_members[] = {
    { nxt_string("root"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    { nxt_string("script"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    { nxt_string("index"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    NXT_CONF_VLDT_NEXT(&nxt_conf_vldt_common_members)
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_go_members[] = {
    { nxt_string("executable"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    NXT_CONF_VLDT_NEXT(&nxt_conf_vldt_common_members)
};


static nxt_conf_vldt_object_t  nxt_conf_vldt_perl_members[] = {
    { nxt_string("script"),
      NXT_CONF_VLDT_STRING,
      NULL,
      NULL },

    NXT_CONF_VLDT_NEXT(&nxt_conf_vldt_common_members)
};


nxt_int_t
nxt_conf_validate(nxt_conf_validation_t *vldt)
{
    nxt_int_t  ret;

    ret = nxt_conf_vldt_type(vldt, NULL, vldt->conf, NXT_CONF_VLDT_OBJECT);

    if (ret != NXT_OK) {
        return ret;
    }

    return nxt_conf_vldt_object(vldt, vldt->conf, nxt_conf_vldt_root_members);
}


#define NXT_CONF_VLDT_ANY_TYPE                                                \
    "either a null, a boolean, an integer, "                                  \
    "a number, a string, an array, or an object"


static nxt_int_t
nxt_conf_vldt_type(nxt_conf_validation_t *vldt, nxt_str_t *name,
    nxt_conf_value_t *value, nxt_conf_vldt_type_t type)
{
    u_char      *p;
    nxt_str_t   expected;
    nxt_bool_t  serial;
    nxt_uint_t  value_type, n, t;
    u_char      buf[sizeof(NXT_CONF_VLDT_ANY_TYPE) - 1];

    static nxt_str_t  type_name[] = {
        nxt_string("a null"),
        nxt_string("a boolean"),
        nxt_string("an integer"),
        nxt_string("a number"),
        nxt_string("a string"),
        nxt_string("an array"),
        nxt_string("an object"),
    };

    value_type = nxt_conf_type(value);

    if ((1 << value_type) & type) {
        return NXT_OK;
    }

    p = buf;

    n = __builtin_popcount(type);

    if (n > 1) {
        p = nxt_cpymem(p, "either ", 7);
    }

    serial = (n > 2);

    for ( ;; ) {
        t = __builtin_ffs(type) - 1;

        p = nxt_cpymem(p, type_name[t].start, type_name[t].length);

        n--;

        if (n == 0) {
            break;
        }

        if (n > 1 || serial) {
            *p++ = ',';
        }

        if (n == 1) {
            p = nxt_cpymem(p, " or", 3);
        }

        *p++ = ' ';

        type = type & ~(1 << t);
    }

    expected.length = p - buf;
    expected.start = buf;

    if (name == NULL) {
        return nxt_conf_vldt_error(vldt,
                                   "The configuration must be %V, but not %V.",
                                   &expected, &type_name[value_type]);
    }

    return nxt_conf_vldt_error(vldt,
                               "The \"%V\" value must be %V, but not %V.",
                               name, &expected, &type_name[value_type]);
}


static nxt_int_t
nxt_conf_vldt_error(nxt_conf_validation_t *vldt, const char *fmt, ...)
{
    u_char   *p, *end;
    size_t   size;
    va_list  args;
    u_char   error[NXT_MAX_ERROR_STR];

    va_start(args, fmt);
    end = nxt_vsprintf(error, error + NXT_MAX_ERROR_STR, fmt, args);
    va_end(args);

    size = end - error;

    p = nxt_mp_nget(vldt->pool, size);
    if (p == NULL) {
        return NXT_ERROR;
    }

    nxt_memcpy(p, error, size);

    vldt->error.length = size;
    vldt->error.start = p;

    return NXT_DECLINED;
}


static nxt_int_t
nxt_conf_vldt_listener(nxt_conf_validation_t *vldt, nxt_str_t *name,
    nxt_conf_value_t *value)
{
    nxt_int_t  ret;

    ret = nxt_conf_vldt_type(vldt, name, value, NXT_CONF_VLDT_OBJECT);

    if (ret != NXT_OK) {
        return ret;
    }

    return nxt_conf_vldt_object(vldt, value, nxt_conf_vldt_listener_members);
}


static nxt_int_t
nxt_conf_vldt_app_name(nxt_conf_validation_t *vldt, nxt_conf_value_t *value,
    void *data)
{
    nxt_str_t         name;
    nxt_conf_value_t  *apps, *app;

    static nxt_str_t  apps_str = nxt_string("applications");

    nxt_conf_get_string(value, &name);

    apps = nxt_conf_get_object_member(vldt->conf, &apps_str, NULL);

    if (nxt_slow_path(apps == NULL)) {
        goto error;
    }

    app = nxt_conf_get_object_member(apps, &name, NULL);

    if (nxt_slow_path(app == NULL)) {
        goto error;
    }

    return NXT_OK;

error:

    return nxt_conf_vldt_error(vldt, "Listening socket is assigned for "
                                     "a non existing application \"%V\".",
                                     &name);
}


static nxt_int_t
nxt_conf_vldt_app(nxt_conf_validation_t *vldt, nxt_str_t *name,
    nxt_conf_value_t *value)
{
    nxt_int_t              ret;
    nxt_str_t              type;
    nxt_thread_t           *thread;
    nxt_conf_value_t       *type_value;
    nxt_app_lang_module_t  *lang;

    static nxt_str_t  type_str = nxt_string("type");

    static void  *members[] = {
        nxt_conf_vldt_python_members,
        nxt_conf_vldt_php_members,
        nxt_conf_vldt_go_members,
        nxt_conf_vldt_perl_members,
    };

    ret = nxt_conf_vldt_type(vldt, name, value, NXT_CONF_VLDT_OBJECT);

    if (ret != NXT_OK) {
        return ret;
    }

    type_value = nxt_conf_get_object_member(value, &type_str, NULL);

    if (type_value == NULL) {
        return nxt_conf_vldt_error(vldt,
                           "Application must have the \"type\" property set.");
    }

    ret = nxt_conf_vldt_type(vldt, &type_str, type_value, NXT_CONF_VLDT_STRING);

    if (ret != NXT_OK) {
        return ret;
    }

    nxt_conf_get_string(type_value, &type);

    thread = nxt_thread();

    lang = nxt_app_lang_module(thread->runtime, &type);
    if (lang == NULL) {
        return nxt_conf_vldt_error(vldt,
                                   "The module to run \"%V\" is not found "
                                   "among the available application modules.",
                                   &type);
    }

    return nxt_conf_vldt_object(vldt, value, members[lang->type]);
}


static nxt_int_t
nxt_conf_vldt_object(nxt_conf_validation_t *vldt, nxt_conf_value_t *value,
    void *data)
{
    uint32_t                index;
    nxt_int_t               ret;
    nxt_str_t               name;
    nxt_conf_value_t        *member;
    nxt_conf_vldt_object_t  *vals;

    index = 0;

    for ( ;; ) {
        member = nxt_conf_next_object_member(value, &name, &index);

        if (member == NULL) {
            return NXT_OK;
        }

        vals = data;

        for ( ;; ) {
            if (vals->name.length == 0) {

                if (vals->data != NULL) {
                    vals = vals->data;
                    continue;
                }

                return nxt_conf_vldt_error(vldt, "Unknown parameter \"%V\".",
                                           &name);
            }

            if (!nxt_strstr_eq(&vals->name, &name)) {
                vals++;
                continue;
            }

            ret = nxt_conf_vldt_type(vldt, &name, member, vals->type);

            if (ret != NXT_OK) {
                return ret;
            }

            if (vals->validator != NULL) {
                ret = vals->validator(vldt, member, vals->data);

                if (ret != NXT_OK) {
                    return ret;
                }
            }

            break;
        }
    }
}


typedef struct {
    int64_t  spare;
    int64_t  max;
    int64_t  idle_timeout;
} nxt_conf_vldt_processes_conf_t;


static nxt_conf_map_t  nxt_conf_vldt_processes_conf_map[] = {
    {
        nxt_string("spare"),
        NXT_CONF_MAP_INT64,
        offsetof(nxt_conf_vldt_processes_conf_t, spare),
    },

    {
        nxt_string("max"),
        NXT_CONF_MAP_INT64,
        offsetof(nxt_conf_vldt_processes_conf_t, max),
    },

    {
        nxt_string("idle_timeout"),
        NXT_CONF_MAP_INT64,
        offsetof(nxt_conf_vldt_processes_conf_t, idle_timeout),
    },
};


static nxt_int_t
nxt_conf_vldt_processes(nxt_conf_validation_t *vldt, nxt_conf_value_t *value,
    void *data)
{
    int64_t                         int_value;
    nxt_int_t                       ret;
    nxt_conf_vldt_processes_conf_t  proc;

    if (nxt_conf_type(value) == NXT_CONF_INTEGER) {
        int_value = nxt_conf_get_integer(value);

        if (int_value < 1) {
            return nxt_conf_vldt_error(vldt, "The \"processes\" number must be "
                                       "equal to or greater than 1.");
        }

        if (int_value > NXT_INT32_T_MAX) {
            return nxt_conf_vldt_error(vldt, "The \"processes\" number must "
                                       "not exceed %d.", NXT_INT32_T_MAX);
        }

        return NXT_OK;
    }

    ret = nxt_conf_vldt_object(vldt, value, data);
    if (ret != NXT_OK) {
        return ret;
    }

    proc.spare = 0;
    proc.max = 1;
    proc.idle_timeout = 15;

    ret = nxt_conf_map_object(vldt->pool, value,
                              nxt_conf_vldt_processes_conf_map,
                              nxt_nitems(nxt_conf_vldt_processes_conf_map),
                              &proc);
    if (ret != NXT_OK) {
        return ret;
    }

    if (proc.spare < 0) {
        return nxt_conf_vldt_error(vldt, "The \"spare\" number must not be "
                                   "negative.");
    }

    if (proc.spare > NXT_INT32_T_MAX) {
        return nxt_conf_vldt_error(vldt, "The \"spare\" number must not "
                                   "exceed %d.", NXT_INT32_T_MAX);
    }

    if (proc.max < 1) {
        return nxt_conf_vldt_error(vldt, "The \"max\" number must be equal "
                                   "to or greater than 1.");
    }

    if (proc.max > NXT_INT32_T_MAX) {
        return nxt_conf_vldt_error(vldt, "The \"max\" number must not "
                                   "exceed %d.", NXT_INT32_T_MAX);
    }

    if (proc.max < proc.spare) {
        return nxt_conf_vldt_error(vldt, "The \"spare\" number must be "
                                   "lower than \"max\".");
    }

    if (proc.idle_timeout < 0) {
        return nxt_conf_vldt_error(vldt, "The \"idle_timeout\" number must not "
                                   "be negative.");
    }

    if (proc.idle_timeout > NXT_INT32_T_MAX / 1000) {
        return nxt_conf_vldt_error(vldt, "The \"idle_timeout\" number must not "
                                   "exceed %d.", NXT_INT32_T_MAX / 1000);
    }

    return NXT_OK;
}


static nxt_int_t
nxt_conf_vldt_object_iterator(nxt_conf_validation_t *vldt,
    nxt_conf_value_t *value, void *data)
{
    uint32_t                index;
    nxt_int_t               ret;
    nxt_str_t               name;
    nxt_conf_value_t        *member;
    nxt_conf_vldt_member_t  validator;

    validator = (nxt_conf_vldt_member_t) data;
    index = 0;

    for ( ;; ) {
        member = nxt_conf_next_object_member(value, &name, &index);

        if (member == NULL) {
            return NXT_OK;
        }

        ret = validator(vldt, &name, member);

        if (ret != NXT_OK) {
            return ret;
        }
    }
}


static nxt_int_t
nxt_conf_vldt_system(nxt_conf_validation_t *vldt, nxt_conf_value_t *value,
    void *data)
{
    size_t                  length;
    nxt_str_t               name;
    nxt_conf_vldt_system_t  validator;
    char                    string[32];

    /* The cast is required by Sun C. */
    validator = (nxt_conf_vldt_system_t) data;

    nxt_conf_get_string(value, &name);

    length = name.length + 1;
    length = nxt_min(length, sizeof(string));

    nxt_cpystrn((u_char *) string, name.start, length);

    return validator(vldt, string);
}


static nxt_int_t
nxt_conf_vldt_user(nxt_conf_validation_t *vldt, char *user)
{
    struct passwd  *pwd;

    nxt_errno = 0;

    pwd = getpwnam(user);

    if (pwd != NULL) {
        return NXT_OK;
    }

    if (nxt_errno == 0) {
        return nxt_conf_vldt_error(vldt, "User \"%s\" is not found.", user);
    }

    return NXT_ERROR;
}


static nxt_int_t
nxt_conf_vldt_group(nxt_conf_validation_t *vldt, char *group)
{
    struct group  *grp;

    nxt_errno = 0;

    grp = getgrnam(group);

    if (grp != NULL) {
        return NXT_OK;
    }

    if (nxt_errno == 0) {
        return nxt_conf_vldt_error(vldt, "Group \"%s\" is not found.", group);
    }

    return NXT_ERROR;
}
