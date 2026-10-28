#include "wiki_rag.h"
#include <stdlib.h>
#include <string.h>

uint64_t wiki_fnv1a(const char *str) {
    uint64_t h = 0xcbf29ce484222325ULL;
    if (!str) return h;
    for (const char *p = str; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t wiki_fnv1a_64_lower(const char *str) {
    uint64_t h = 0xcbf29ce484222325ULL;
    if (!str) return h;

    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        if (*p < 0x80) {
            uint8_t c = *p;
            if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
            h ^= c;
            h *= 0x100000001b3ULL;
            p++;
        } else if (*p == 0xD0 && *(p + 1) != 0) {
            uint8_t b2 = *(p + 1);
            if (b2 >= 0x90 && b2 <= 0x9F) {
                // А..П -> а..п (0xD0 0xB0 .. 0xD0 0xBF)
                h ^= 0xD0; h *= 0x100000001b3ULL;
                h ^= (uint8_t)(b2 + 0x20); h *= 0x100000001b3ULL;
            } else if (b2 >= 0xA0 && b2 <= 0xAF) {
                // Р..Я -> р..я (0xD1 0x80 .. 0xD1 0x8F)
                h ^= 0xD1; h *= 0x100000001b3ULL;
                h ^= (uint8_t)(b2 - 0x20); h *= 0x100000001b3ULL;
            } else if (b2 == 0x81) {
                // Ё -> ё (0xD1 0x91)
                h ^= 0xD1; h *= 0x100000001b3ULL;
                h ^= 0x91; h *= 0x100000001b3ULL;
            } else {
                h ^= 0xD0; h *= 0x100000001b3ULL;
                h ^= b2; h *= 0x100000001b3ULL;
            }
            p += 2;
        } else {
            h ^= *p;
            h *= 0x100000001b3ULL;
            p++;
        }
    }
    return h;
}

int wiki_open(WikiIndex *idx, const char *path) {
    if (!idx || !path) return -1;
    idx->f = fopen(path, "rb");
    if (!idx->f) return -1;

    uint32_t magic = 0;
    if (fread(&magic, sizeof(uint32_t), 1, idx->f) != 1 || magic != WIKI_MAGIC) {
        fclose(idx->f);
        idx->f = NULL;
        return -2;
    }

    if (fread(&idx->entry_count, sizeof(uint32_t), 1, idx->f) != 1) {
        fclose(idx->f);
        idx->f = NULL;
        return -3;
    }

    // Index table starts immediately after 8-byte header
    idx->index_start = 8;
    return 0;
}

void wiki_close(WikiIndex *idx) {
    if (idx && idx->f) {
        fclose(idx->f);
        idx->f = NULL;
        idx->entry_count = 0;
    }
}

int wiki_search(WikiIndex *idx, uint64_t hash, char *out_buf, size_t buf_size) {
    if (!idx || !idx->f || idx->entry_count == 0 || !out_buf || buf_size == 0) {
        return -1;
    }

    int32_t low = 0;
    int32_t high = (int32_t)idx->entry_count - 1;

    while (low <= high) {
        int32_t mid = low + (high - low) / 2;
        WikiEntry entry;
        if (fseek(idx->f, idx->index_start + (mid * (long)sizeof(WikiEntry)), SEEK_SET) != 0) {
            break;
        }
        if (fread(&entry, sizeof(WikiEntry), 1, idx->f) != 1) {
            break;
        }

        if (entry.hash == hash) {
            if (fseek(idx->f, entry.offset, SEEK_SET) != 0) {
                return -1;
            }
            size_t to_read = (entry.length < buf_size - 1) ? entry.length : buf_size - 1;
            size_t n = fread(out_buf, 1, to_read, idx->f);
            out_buf[n] = '\0';
            return (int)n;
        } else if (entry.hash < hash) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return -1;
}

bool wiki_rag_lookup_by_hash(const char *path, uint64_t hash, char *out_buf, size_t buf_size) {
    if (!path || !out_buf || buf_size == 0) return false;
    WikiIndex idx;
    if (wiki_open(&idx, path) != 0) return false;
    int res = wiki_search(&idx, hash, out_buf, buf_size);
    wiki_close(&idx);
    return (res > 0);
}
