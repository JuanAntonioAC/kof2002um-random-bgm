#include "router.h"

static void bytes_set(void* dst, unsigned char value, unsigned long long n) {
    volatile unsigned char* d = (volatile unsigned char*)dst;
    unsigned long long i;
    for (i = 0; i < n; ++i) d[i] = value;
}

static void bytes_copy(void* dst, const void* src, unsigned long long n) {
    volatile unsigned char* d = (volatile unsigned char*)dst;
    const volatile unsigned char* s = (const volatile unsigned char*)src;
    unsigned long long i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

static int wlen(const Wchar* text) {
    int n = 0;
    if (!text) return 0;
    while (text[n]) ++n;
    return n;
}

static void wclear(Wchar* text, int cap) {
    int i;
    for (i = 0; i < cap; ++i) text[i] = 0;
}

static void wcopy(Wchar* dst, int cap, const Wchar* src) {
    int i = 0;
    if (cap <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static Wchar wlower(Wchar ch) {
    if (ch >= 'A' && ch <= 'Z') return (Wchar)(ch - 'A' + 'a');
    return ch;
}

static Wchar wupper(Wchar ch) {
    if (ch >= 'a' && ch <= 'z') return (Wchar)(ch - 'a' + 'A');
    return ch;
}

static int weq_i(const Wchar* a, const Wchar* b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (wupper(a[i]) != wupper(b[i])) return 0;
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int contains_ci(const Wchar* hay, int hay_len, const Wchar* needle) {
    int nlen = wlen(needle);
    int i, j;
    if (nlen == 0 || hay_len < nlen) return -1;
    for (i = 0; i <= hay_len - nlen; ++i) {
        for (j = 0; j < nlen; ++j) {
            if (wlower(hay[i + j]) != needle[j]) break;
        }
        if (j == nlen) return i;
    }
    return -1;
}

int id_list_contains(const IdList* list, const Wchar* id) {
    int i;
    for (i = 0; i < list->count; ++i) {
        if (weq_i(list->ids[i], id)) return 1;
    }
    return 0;
}

static void add_id(IdList* list, const Wchar* id) {
    if (list->count >= R_MAX_IDS) return;
    wcopy(list->ids[list->count], R_ID_CHARS, id);
    list->count++;
}

void router_init(Router* router, const Settings* settings, int (*rng)(int)) {
    bytes_set(router, 0, sizeof(*router));
    bytes_copy(&router->settings, settings, sizeof(*settings));
    router->rng = rng;
}

void router_set_last(Router* router, const Wchar* bank, const Wchar* id) {
    wcopy(router->last_bank, 16, bank);
    wcopy(router->last_id, R_ID_CHARS, id);
}

static int consume_ogg(const Wchar* file, Wchar* stem, int stem_cap) {
    int n = wlen(file);
    if (n < 5 || file[n - 4] != '.') return 0;
    if (wlower(file[n - 3]) != 'o' || wlower(file[n - 2]) != 'g' || wlower(file[n - 1]) != 'g') return 0;
    if (n - 4 >= stem_cap) return 0;
    wcopy(stem, stem_cap, file);
    stem[n - 4] = 0;
    return 1;
}

static int parse_stem(const Wchar* bank, const Wchar* stem, Wchar* id, int* loop) {
    Wchar upper[32];
    int n, i;
    wclear(upper, 32);
    wcopy(upper, 32, stem);
    n = wlen(upper);
    for (i = 0; i < n; ++i) upper[i] = wupper(upper[i]);
    *loop = 0;
    if (n > 4 && upper[n - 4] == 'L' && upper[n - 3] == 'O' && upper[n - 2] == 'O' && upper[n - 1] == 'P') {
        *loop = 1;
        upper[n - 4] = 0;
        n -= 4;
    }
    if (bank[0] == 'a') {
        if (n != 5 || upper[0] != 'B' || upper[1] != 'G' || upper[2] != 'M') return 0;
        if (upper[3] < '0' || upper[3] > '9' || upper[4] < '0' || upper[4] > '9') return 0;
        wcopy(id, R_ID_CHARS, upper);
        return 1;
    }
    if (n == 5 && upper[0] == 'D' && upper[1] == 'U' && upper[2] == 'M' && upper[3] == 'M' && upper[4] == 'Y') {
        wcopy(id, R_ID_CHARS, upper);
        return 1;
    }
    if (n != 2) return 0;
    for (i = 0; i < 2; ++i) {
        int hex = (upper[i] >= '0' && upper[i] <= '9') || (upper[i] >= 'A' && upper[i] <= 'F');
        if (!hex) return 0;
    }
    wcopy(id, R_ID_CHARS, upper);
    return 1;
}

static int parse_track(const Wchar* path, Wchar* bank, Wchar* id, int* loop) {
    int len = wlen(path);
    int folder;
    int name_pos;
    int folder_len;
    Wchar file[80];
    Wchar stem[32];
    if (len <= 0 || len >= R_PATH_CHARS) return 0;
    folder = contains_ci(path, len, L"um48000hz");
    if (folder >= 0) {
        wcopy(bank, 16, L"arrange");
        folder_len = 10;
    } else {
        folder = contains_ci(path, len, L"48000hz");
        if (folder < 0) return 0;
        wcopy(bank, 16, L"original");
        folder_len = 7;
    }
    name_pos = folder + folder_len;
    if (name_pos < len && (path[name_pos] == '\\' || path[name_pos] == '/')) ++name_pos;
    if (name_pos >= len) return 0;
    wcopy(file, 80, path + name_pos);
    {
        int i;
        int last = -1;
        int flen = wlen(file);
        for (i = 0; i < flen; ++i) {
            if (file[i] == '\\' || file[i] == '/') last = i;
        }
        if (last >= 0) wcopy(file, 80, file + last + 1);
    }
    if (!consume_ogg(file, stem, 32)) return 0;
    return parse_stem(bank, stem, id, loop);
}

static void replace_file(const Wchar* path, const Wchar* file_name, Wchar* dest, int cap) {
    int len = wlen(path);
    int slash = -1;
    int i;
    int name_len = wlen(file_name);
    for (i = 0; i < len; ++i) {
        if (path[i] == '\\' || path[i] == '/') slash = i;
    }
    wclear(dest, cap);
    if (slash < 0) {
        wcopy(dest, cap, file_name);
        return;
    }
    if (slash + 1 + name_len + 1 > cap) {
        wcopy(dest, cap, path);
        return;
    }
    for (i = 0; i <= slash; ++i) dest[i] = path[i];
    for (i = 0; i < name_len; ++i) dest[slash + 1 + i] = file_name[i];
    dest[slash + 1 + name_len] = 0;
}

static void pick(Router* router, const IdList* pool, Wchar* dest) {
    int n = pool->count;
    int index = 0;
    if (n > 1 && router->rng) {
        index = router->rng(n);
        if (index < 0 || index >= n) index = 0;
    }
    if (router->settings.avoid_repeat && n > 1 && weq_i(pool->ids[index], router->last_id)) {
        index = (index + 1) % n;
    }
    wcopy(dest, R_ID_CHARS, pool->ids[index]);
}

void router_route(Router* router, const Wchar* path, RouteResult* result) {
    Wchar bank[16];
    Wchar id[R_ID_CHARS];
    int loop = 0;
    const IdList* boundary;
    const IdList* source;
    const IdList* pool;
    const IdList* loops;
    Wchar file_name[24];
    int target_loop;
    int name_len;

    bytes_set(result, 0, sizeof(*result));
    wcopy(result->path, R_PATH_CHARS, path);
    if (!router->settings.enabled) {
        result->action = ROUTE_IGNORE;
        return;
    }
    wclear(bank, 16);
    wclear(id, R_ID_CHARS);
    if (!parse_track(path, bank, id, &loop)) {
        result->action = ROUTE_IGNORE;
        return;
    }
    wcopy(result->bank, 16, bank);
    wcopy(result->requested_id, R_ID_CHARS, id);
    result->requested_loop = loop;

    if (bank[0] == 'a') {
        boundary = &router->settings.arrange_boundary;
        source = &router->settings.arrange_source;
        pool = &router->settings.arrange_pool;
        loops = &router->settings.arrange_loop;
    } else {
        boundary = &router->settings.original_boundary;
        source = &router->settings.original_source;
        pool = &router->settings.original_pool;
        loops = &router->settings.original_loop;
    }

    if (id_list_contains(boundary, id)) {
        wclear(router->locked_id, R_ID_CHARS);
        wclear(router->locked_bank, 16);
        result->action = ROUTE_BOUNDARY;
        return;
    }
    if (!id_list_contains(source, id) || pool->count == 0) {
        result->action = ROUTE_PASS;
        return;
    }

    if (router->locked_id[0] == 0 || !weq_i(router->locked_bank, bank)) {
        pick(router, pool, router->locked_id);
        wcopy(router->locked_bank, 16, bank);
        wcopy(router->last_id, R_ID_CHARS, router->locked_id);
        wcopy(router->last_bank, 16, bank);
        result->action = ROUTE_LOCK;
    } else {
        result->action = ROUTE_HOLD;
    }

    wcopy(result->chosen_id, R_ID_CHARS, router->locked_id);
    target_loop = loop && id_list_contains(loops, router->locked_id);
    wclear(file_name, 24);
    wcopy(file_name, 24, router->locked_id);
    name_len = wlen(file_name);
    if (target_loop && name_len + 8 < 24) {
        file_name[name_len++] = 'L';
        file_name[name_len++] = 'O';
        file_name[name_len++] = 'O';
        file_name[name_len++] = 'P';
        file_name[name_len] = 0;
    }
    if (name_len + 5 < 24) {
        file_name[name_len++] = '.';
        file_name[name_len++] = 'o';
        file_name[name_len++] = 'g';
        file_name[name_len++] = 'g';
        file_name[name_len] = 0;
    }
    replace_file(path, file_name, result->path, R_PATH_CHARS);
}

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static void trim(const char** begin, const char** end) {
    while (*begin < *end && is_space(**begin)) ++*begin;
    while (*end > *begin && is_space(*(*end - 1))) --*end;
}

static int eq_token(const char* begin, const char* end, const char* lit) {
    int i = 0;
    while (lit[i] && begin + i < end) {
        char ch = begin[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (ch != lit[i]) return 0;
        ++i;
    }
    return lit[i] == 0 && begin + i == end;
}

static int value_true(const char* begin, const char* end) {
    return eq_token(begin, end, "1") || eq_token(begin, end, "true");
}

static void ascii_to_wide(Wchar* dst, int cap, const char* begin, const char* end) {
    int i = 0;
    wclear(dst, cap);
    while (begin < end && i + 1 < cap) {
        dst[i++] = (Wchar)(unsigned char)(*begin++);
    }
    dst[i] = 0;
}

static IdList* section_list(Settings* settings, const char* begin, const char* end) {
    if (eq_token(begin, end, "arrange_source")) return &settings->arrange_source;
    if (eq_token(begin, end, "arrange_pool")) return &settings->arrange_pool;
    if (eq_token(begin, end, "arrange_boundary")) return &settings->arrange_boundary;
    if (eq_token(begin, end, "arrange_loop")) return &settings->arrange_loop;
    if (eq_token(begin, end, "original_source")) return &settings->original_source;
    if (eq_token(begin, end, "original_pool")) return &settings->original_pool;
    if (eq_token(begin, end, "original_boundary")) return &settings->original_boundary;
    if (eq_token(begin, end, "original_loop")) return &settings->original_loop;
    return 0;
}

int parse_settings(const char* text, int length, Settings* settings) {
    int i = 0;
    char section[40];
    int section_len = 0;
    if (!text || length < 0) return 0;
    bytes_set(settings, 0, sizeof(*settings));
    settings->enabled = 1;
    settings->avoid_repeat = 1;
    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        i = 3;
    section[0] = 0;
    while (i < length) {
        int line = i;
        const char* begin;
        const char* end;
        while (i < length && text[i] != '\n') ++i;
        begin = text + line;
        end = text + i;
        if (i < length && text[i] == '\n') ++i;
        trim(&begin, &end);
        if (begin == end || *begin == '#' || *begin == ';') continue;
        if (begin < end && *begin == '[' && *(end - 1) == ']') {
            const char* p = begin + 1;
            const char* stop = end - 1;
            int n = 0;
            while (n < 39 && p < stop) {
                section[n++] = *p++;
            }
            section[n] = 0;
            section_len = n;
            continue;
        }
        if (section_len == 0) {
            const char* eq = begin;
            while (eq < end && *eq != '=') ++eq;
            if (eq < end) {
                const char* key_b = begin;
                const char* key_e = eq;
                const char* val_b = eq + 1;
                const char* val_e = end;
                trim(&key_b, &key_e);
                trim(&val_b, &val_e);
                if (eq_token(key_b, key_e, "enabled")) settings->enabled = value_true(val_b, val_e);
                else if (eq_token(key_b, key_e, "avoid_repeat")) settings->avoid_repeat = value_true(val_b, val_e);
            }
            continue;
        }
        {
            IdList* list = section_list(settings, section, section + section_len);
            Wchar id[R_ID_CHARS];
            if (!list) continue;
            ascii_to_wide(id, R_ID_CHARS, begin, end);
            if (id[0]) add_id(list, id);
        }
    }
    return 1;
}
