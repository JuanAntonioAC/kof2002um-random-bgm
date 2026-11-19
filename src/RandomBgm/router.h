#pragma once

typedef unsigned short Wchar;

#define R_MAX_IDS 40
#define R_ID_CHARS 12
#define R_PATH_CHARS 300

typedef struct IdList {
    int count;
    Wchar ids[R_MAX_IDS][R_ID_CHARS];
} IdList;

typedef struct Settings {
    int enabled;
    int avoid_repeat;
    IdList arrange_source;
    IdList arrange_pool;
    IdList arrange_boundary;
    IdList arrange_loop;
    IdList original_source;
    IdList original_pool;
    IdList original_boundary;
    IdList original_loop;
} Settings;

typedef enum RouteAction {
    ROUTE_IGNORE = 0,
    ROUTE_PASS = 1,
    ROUTE_BOUNDARY = 2,
    ROUTE_LOCK = 3,
    ROUTE_HOLD = 4
} RouteAction;

typedef struct RouteResult {
    RouteAction action;
    Wchar path[R_PATH_CHARS];
    Wchar bank[16];
    Wchar requested_id[R_ID_CHARS];
    Wchar chosen_id[R_ID_CHARS];
    int requested_loop;
} RouteResult;

typedef struct Router {
    Settings settings;
    int (*rng)(int n);
    Wchar locked_id[R_ID_CHARS];
    Wchar locked_bank[16];
    Wchar last_id[R_ID_CHARS];
    Wchar last_bank[16];
} Router;

void router_init(Router* router, const Settings* settings, int (*rng)(int));
void router_set_last(Router* router, const Wchar* bank, const Wchar* id);
void router_route(Router* router, const Wchar* path, RouteResult* result);
int parse_settings(const char* text, int length, Settings* settings);
int id_list_contains(const IdList* list, const Wchar* id);
