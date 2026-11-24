#ifndef SKILL_FINGERPRINT_H
#define SKILL_FINGERPRINT_H

#include <stddef.h>
#include <time.h>

int hub_compute_skill_hash(const char *dir_path, char *out_hash_hex, size_t max_len, time_t *out_mtime);

#endif
