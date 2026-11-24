#ifndef SYNC_WEBDAV_LISTING_H
#define SYNC_WEBDAV_LISTING_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define DAV_URL_CAP 4096

typedef struct {
    char name[256];
    bool collection;
    time_t modified;
} dav_entry_t;

typedef struct {
    dav_entry_t *entries;
    size_t count;
    time_t modified;
} dav_listing_t;

bool dav_parse_listing(const char *xml, size_t size, const char *url, dav_listing_t *listing);

#endif
