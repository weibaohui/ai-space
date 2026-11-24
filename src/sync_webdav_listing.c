#include "sync_webdav_listing.h"
#include "file_tree.h"
#include "skill_metadata.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *xml_tag(const char *p, const char *end, const char *name, bool closing,
                           const char **after) {
    size_t wanted = strlen(name);
    while (p < end) {
        p = memchr(p, '<', (size_t)(end - p));
        if (!p || p + 1 >= end) break;
        const char *tag = p++;
        if (end - p >= 3 && memcmp(p, "!--", 3) == 0) {
            const char *tail = strstr(p + 3, "-->");
            if (!tail || tail >= end) break;
            p = tail + 3;
            continue;
        }
        bool close = *p == '/';
        if (close) p++;
        const char *local = p;
        while (p < end && !isspace((unsigned char)*p) && *p != '>' && *p != '/' && *p != '<') {
            if (*p == ':') local = p + 1;
            p++;
        }
        if (close == closing && (size_t)(p - local) == wanted && memcmp(local, name, wanted) == 0) {
            const char *finish = memchr(p, '>', (size_t)(end - p));
            if (!finish) break;
            if (after) *after = finish + 1;
            return tag;
        }
    }
    return NULL;
}

static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool decode_path(const char *input, const char *end, char *out, size_t cap) {
    size_t n = 0;
    while (input < end) {
        unsigned char c = *input++;
        if (c == '%') {
            if (end - input < 2) return false;
            int hi = hex_digit(input[0]), lo = hex_digit(input[1]);
            if (hi < 0 || lo < 0) return false;
            c = (unsigned char)(hi * 16 + lo);
            input += 2;
        } else if (c == '&') {
            static const struct { const char *ref; char value; } entities[] = {
                {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''},
            };
            bool found = false;
            for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
                size_t len = strlen(entities[i].ref);
                if ((size_t)(end - input) >= len && memcmp(input, entities[i].ref, len) == 0) {
                    c = (unsigned char)entities[i].value;
                    input += len;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (c < 32 || c == 127 || n + 1 >= cap) return false;
        out[n++] = (char)c;
    }
    while (n > 1 && out[n - 1] == '/') n--;
    out[n] = '\0';
    return true;
}

static const char *url_path(const char *url) {
    const char *scheme = strstr(url, "://");
    if (!scheme) return url;
    const char *path = strchr(scheme + 3, '/');
    return path ? path : "/";
}

static time_t dav_modified(const char *p, const char *end) {
    const char *value;
    if (!xml_tag(p, end, "getlastmodified", false, &value)) return 0;
    int day, year, hour, min, sec;
    char month[4];
    if (sscanf(value, "%*3s, %d %3s %d %d:%d:%d", &day, month, &year, &hour, &min, &sec) != 6 ||
        year < 1970 || year > 9999 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) return 0;
    static const char months[][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const int starts[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int m = 0;
    while (m < 12 && strcmp(month, months[m]) != 0) m++;
    if (m == 12) return 0;
    int previous = year - 1;
    int days = (year - 1970) * 365 + previous / 4 - previous / 100 + previous / 400 - 477;
    if (m >= 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) days++;
    return (time_t)((int64_t)(days + starts[m] + day - 1) * 86400 + hour * 3600 + min * 60 + sec);
}

/* 解析时仅收录请求目录的直接子项，并验证响应中包含父目录。 */
bool dav_parse_listing(const char *xml, size_t size, const char *url, dav_listing_t *listing) {
    char parent[DAV_URL_CAP];
    const char *path = url_path(url);
    if (!decode_path(path, path + strlen(path), parent, sizeof(parent))) return false;
    size_t parent_len = strlen(parent), capacity = 0;
    const char *p = xml, *end = xml + size;
    bool has_root = false;
    while (xml_tag(p, end, "response", false, &p)) {
        const char *next;
        const char *response_end = xml_tag(p, end, "response", true, &next);
        const char *value;
        if (!response_end || !xml_tag(p, response_end, "href", false, &value)) return false;
        const char *href_end = xml_tag(value, response_end, "href", true, NULL);
        if (!href_end || (size_t)(href_end - value) >= DAV_URL_CAP) return false;
        char raw[DAV_URL_CAP], decoded[DAV_URL_CAP];
        size_t len = (size_t)(href_end - value);
        memcpy(raw, value, len);
        raw[len] = '\0';
        const char *href = url_path(raw);
        if (!decode_path(href, raw + len, decoded, sizeof(decoded))) return false;

        const char *properties;
        bool collection = false;
        if (xml_tag(p, response_end, "resourcetype", false, &properties)) {
            const char *property_end = xml_tag(properties, response_end, "resourcetype", true, NULL);
            if (property_end) collection = xml_tag(properties, property_end, "collection", false, NULL) != NULL;
        }
        time_t modified = dav_modified(p, response_end);
        p = next;

        if (strcmp(parent, decoded) == 0) {
            has_root = collection;
            listing->modified = modified;
            continue;
        }
        if (strncmp(parent, decoded, parent_len) != 0) continue;
        const char *name = decoded + parent_len;
        if (strcmp(parent, "/") != 0) {
            if (*name++ != '/') continue;
        }
        if (!sync_valid_component(name) || skill_skip_name(name)) continue;
        if (strlen(name) >= sizeof(listing->entries[0].name)) return false;

        if (listing->count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            dav_entry_t *entries = realloc(listing->entries, capacity * sizeof(*entries));
            if (!entries) return false;
            listing->entries = entries;
        }
        dav_entry_t *entry = &listing->entries[listing->count++];
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        entry->collection = collection;
        entry->modified = modified;
    }
    return has_root;
}
