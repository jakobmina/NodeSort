/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L /* fseeko / ftello */
#  define _FILE_OFFSET_BITS 64
#endif

#include "array_io.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define ns_fseek _fseeki64
#  define ns_ftell _ftelli64
#else
#  include <sys/types.h>
#  define ns_fseek fseeko
#  define ns_ftell ftello
#endif

static int fail(char *err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, ERR_LEN, fmt, ap);
    va_end(ap);
    return -1;
}

static int host_is_little_endian(void) {
    const uint16_t one = 1;
    unsigned char b;
    memcpy(&b, &one, 1);
    return b == 1;
}

size_t dtype_size(dtype t) {
    switch (t) {
    case DT_F32:
    case DT_I32:
    case DT_U32: return 4;
    case DT_F64:
    case DT_U64: return 8;
    default: return 0;
    }
}

const char *dtype_name(dtype t) {
    switch (t) {
    case DT_F32: return "float32";
    case DT_F64: return "float64";
    case DT_I32: return "int32";
    case DT_U32: return "uint32";
    case DT_U64: return "uint64";
    default: return "none";
    }
}

static const char *dtype_descr(dtype t) {
    switch (t) {
    case DT_F32: return "<f4";
    case DT_F64: return "<f8";
    case DT_I32: return "<i4";
    case DT_U32: return "<u4";
    case DT_U64: return "<u8";
    default: return "";
    }
}

int dtype_parse(const char *s, dtype *out) {
    static const struct {
        const char *name;
        dtype t;
    } names[] = {{"f32", DT_F32}, {"f64", DT_F64}, {"i32", DT_I32}, {"u32", DT_U32}, {"u64", DT_U64}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(s, names[i].name) == 0) {
            *out = names[i].t;
            return 1;
        }
    return 0;
}

static const char *extension(const char *path) {
    const char *dot = strrchr(path, '.');
    const char *sep1 = strrchr(path, '/'), *sep2 = strrchr(path, '\\');
    if (!dot || (sep1 && sep1 > dot) || (sep2 && sep2 > dot)) return "";
    return dot + 1;
}

static int ext_equals(const char *path, const char *ext) {
    const char *e = extension(path);
    for (; *e && *ext; e++, ext++)
        if (tolower((unsigned char)*e) != *ext) return 0;
    return *e == '\0' && *ext == '\0';
}

int path_is_npy(const char *path) { return ext_equals(path, "npy"); }

int dtype_from_extension(const char *path, dtype *out) {
    static const char *const exts[] = {"f32", "f64", "i32", "u32", "u64"};
    for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++)
        if (ext_equals(path, exts[i])) return dtype_parse(exts[i], out);
    return 0;
}

void array_free(array_t *a) {
    free(a->data);
    a->data = NULL;
}

/* ── Reading ───────────────────────────────────────────────────────────── */
static int file_size(FILE *f, uint64_t *size) {
    if (ns_fseek(f, 0, SEEK_END) != 0) return 0;
    long long end = (long long)ns_ftell(f);
    if (end < 0 || ns_fseek(f, 0, SEEK_SET) != 0) return 0;
    *size = (uint64_t)end;
    return 1;
}

static int read_payload(FILE *f, const char *path, array_t *a, uint64_t available, char *err) {
    size_t esize = dtype_size(a->type);
    if (a->count > SIZE_MAX / esize) return fail(err, "%s: array too large", path);
    size_t bytes = a->count * esize;
    if ((uint64_t)bytes > available)
        return fail(err, "%s: file is truncated (expected %zu data bytes, found %llu)", path, bytes,
                    (unsigned long long)available);
    a->data = malloc(bytes ? bytes : 1);
    if (!a->data) return fail(err, "%s: out of memory (%zu bytes)", path, bytes);
    if (bytes && fread(a->data, 1, bytes, f) != bytes) {
        array_free(a);
        return fail(err, "%s: read error", path);
    }
    return 0;
}

/* Finds `'key':` in a .npy header dict and returns a pointer to the value after the colon. */
static const char *dict_value(const char *hdr, const char *key) {
    char pattern[32];
    snprintf(pattern, sizeof pattern, "'%s'", key);
    const char *p = strstr(hdr, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ') p++;
    return p;
}

static int parse_npy_header(const char *hdr, const char *path, array_t *a, char *err) {
    const char *descr = dict_value(hdr, "descr");
    const char *fortran = dict_value(hdr, "fortran_order");
    const char *shape = dict_value(hdr, "shape");
    if (!descr || !fortran || !shape || (*descr != '\'' && *descr != '"'))
        return fail(err, "%s: malformed .npy header", path);

    char d[16] = {0};
    const char *end = strchr(descr + 1, *descr);
    if (!end || (size_t)(end - descr - 1) >= sizeof d)
        return fail(err, "%s: dtype not supported (use float32, float64, int32 or uint32)", path);
    memcpy(d, descr + 1, (size_t)(end - descr - 1));
    static const dtype all[] = {DT_F32, DT_F64, DT_I32, DT_U32, DT_U64};
    a->type = DT_NONE;
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
        if (strcmp(d, dtype_descr(all[i])) == 0) a->type = all[i];
    if (a->type == DT_NONE) {
        if (d[0] == '>')
            return fail(err, "%s: big-endian dtype '%s' not supported (use arr.astype('<%s'))", path,
                        d, d + 1);
        return fail(err, "%s: dtype '%s' not supported (use float32, float64, int32 or uint32)",
                    path, d);
    }

    int fortran_order = strncmp(fortran, "True", 4) == 0;

    if (*shape != '(') return fail(err, "%s: malformed .npy shape", path);
    size_t dims[3];
    size_t ndim = 0;
    const char *p = shape + 1;
    for (;;) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ')') break;
        if (!isdigit((unsigned char)*p)) return fail(err, "%s: malformed .npy shape", path);
        char *stop;
        unsigned long long v = strtoull(p, &stop, 10);
        if (ndim == 3) return fail(err, "%s: arrays with more than 2 dimensions not supported", path);
        dims[ndim++] = (size_t)v;
        p = stop;
    }
    if (ndim == 0) return fail(err, "%s: scalar .npy files are not supported", path);
    if (ndim > 2) return fail(err, "%s: arrays with more than 2 dimensions not supported", path);

    a->ndim = ndim;
    a->rows = dims[0];
    a->cols = ndim == 2 ? dims[1] : 1;
    if (ndim == 2 && fortran_order && a->rows > 1 && a->cols > 1)
        return fail(err, "%s: Fortran-ordered matrix not supported (use np.ascontiguousarray)",
                    path);
    if (a->cols != 0 && a->rows > SIZE_MAX / a->cols)
        return fail(err, "%s: array too large", path);
    a->count = a->rows * a->cols;
    return 0;
}

static int read_npy(FILE *f, const char *path, uint64_t fsize, array_t *a, char *err) {
    unsigned char pre[12];
    if (fsize < 10 || fread(pre, 1, 8, f) != 8 || memcmp(pre, "\x93NUMPY", 6) != 0)
        return fail(err, "%s: not a .npy file (bad magic)", path);
    unsigned major = pre[6];
    size_t hlen, prefix;
    if (major == 1) {
        if (fread(pre + 8, 1, 2, f) != 2) return fail(err, "%s: truncated .npy header", path);
        hlen = (size_t)pre[8] | ((size_t)pre[9] << 8);
        prefix = 10;
    } else if (major == 2 || major == 3) {
        if (fread(pre + 8, 1, 4, f) != 4) return fail(err, "%s: truncated .npy header", path);
        hlen = (size_t)pre[8] | ((size_t)pre[9] << 8) | ((size_t)pre[10] << 16) |
               ((size_t)pre[11] << 24);
        prefix = 12;
    } else {
        return fail(err, "%s: unsupported .npy format version %u", path, major);
    }
    if ((uint64_t)prefix + hlen > fsize || hlen > (1u << 20))
        return fail(err, "%s: truncated .npy header", path);

    char *hdr = (char *)malloc(hlen + 1);
    if (!hdr) return fail(err, "%s: out of memory", path);
    if (fread(hdr, 1, hlen, f) != hlen) {
        free(hdr);
        return fail(err, "%s: truncated .npy header", path);
    }
    hdr[hlen] = '\0';
    int rc = parse_npy_header(hdr, path, a, err);
    free(hdr);
    if (rc != 0) return rc;
    a->from_npy = 1;
    return read_payload(f, path, a, fsize - prefix - hlen, err);
}

static int read_raw(FILE *f, const char *path, uint64_t fsize, dtype type, size_t cols, array_t *a,
                    char *err) {
    if (type == DT_NONE && !dtype_from_extension(path, &type))
        return fail(err,
                    "%s: unknown raw type; use a .f32/.f64/.i32/.u32 extension or --type", path);
    size_t esize = dtype_size(type);
    if (fsize % esize != 0)
        return fail(err, "%s: size %llu is not a multiple of %zu bytes (%s)", path,
                    (unsigned long long)fsize, esize, dtype_name(type));
    uint64_t count = fsize / esize;
    if (count > SIZE_MAX) return fail(err, "%s: array too large", path);
    a->type = type;
    a->count = (size_t)count;
    if (cols == 0) {
        a->ndim = 1;
        a->rows = a->count;
        a->cols = 1;
    } else {
        if (a->count % cols != 0)
            return fail(err, "%s: %zu values do not split into rows of %zu", path, a->count, cols);
        a->ndim = 2;
        a->rows = a->count / cols;
        a->cols = cols;
    }
    a->from_npy = 0;
    return read_payload(f, path, a, fsize, err);
}

int array_read(const char *path, dtype raw_type, size_t raw_cols, array_t *out, char *err) {
    memset(out, 0, sizeof *out);
    if (!host_is_little_endian()) return fail(err, "big-endian hosts are not supported");
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, "%s: cannot open file", path);
    uint64_t fsize;
    int rc;
    if (!file_size(f, &fsize))
        rc = fail(err, "%s: cannot determine file size", path);
    else if (path_is_npy(path))
        rc = read_npy(f, path, fsize, out, err);
    else
        rc = read_raw(f, path, fsize, raw_type, raw_cols, out, err);
    fclose(f);
    return rc;
}

/* ── Writing ───────────────────────────────────────────────────────────── */
static int write_npy_header(FILE *f, const char *descr, const char *shape) {
    char dict[256];
    int len = snprintf(dict, sizeof dict, "{'descr': %s, 'fortran_order': False, 'shape': %s, }",
                       descr, shape);
    if (len < 0 || (size_t)len >= sizeof dict) return 0;
    size_t total = 10 + (size_t)len + 1; /* prefix + dict + '\n' */
    size_t pad = (64 - total % 64) % 64;
    size_t hlen = (size_t)len + pad + 1;
    unsigned char pre[10] = {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0, (unsigned char)(hlen & 0xFF),
                             (unsigned char)(hlen >> 8)};
    if (fwrite(pre, 1, 10, f) != 10 || fwrite(dict, 1, (size_t)len, f) != (size_t)len) return 0;
    for (size_t i = 0; i < pad; i++)
        if (fputc(' ', f) == EOF) return 0;
    return fputc('\n', f) != EOF;
}

static int finish_write(FILE *f, const char *path, int ok, char *err) {
    if (fclose(f) != 0) ok = 0;
    if (!ok) {
        remove(path);
        return fail(err, "%s: write error", path);
    }
    return 0;
}

int array_write(const char *path, const array_t *a, int as_npy, char *err) {
    FILE *f = fopen(path, "wb");
    if (!f) return fail(err, "%s: cannot create file", path);
    int ok = 1;
    if (as_npy) {
        char descr[16], shape[64];
        snprintf(descr, sizeof descr, "'%s'", dtype_descr(a->type));
        if (a->ndim == 2)
            snprintf(shape, sizeof shape, "(%zu, %zu)", a->rows, a->cols);
        else
            snprintf(shape, sizeof shape, "(%zu,)", a->count);
        ok = write_npy_header(f, descr, shape);
    }
    size_t bytes = a->count * dtype_size(a->type);
    if (ok && bytes) ok = fwrite(a->data, 1, bytes, f) == bytes;
    return finish_write(f, path, ok, err);
}

int npy_write_id_score(const char *path, const void *ids, dtype id_type, const float *scores,
                       size_t n, char *err) {
    FILE *f = fopen(path, "wb");
    if (!f) return fail(err, "%s: cannot create file", path);
    char descr[64], shape[32];
    snprintf(descr, sizeof descr, "[('id', '%s'), ('score', '<f4')]", dtype_descr(id_type));
    snprintf(shape, sizeof shape, "(%zu,)", n);
    int ok = write_npy_header(f, descr, shape);
    size_t isize = dtype_size(id_type);
    for (size_t i = 0; ok && i < n; i++) {
        ok = fwrite((const unsigned char *)ids + i * isize, 1, isize, f) == isize &&
             fwrite(&scores[i], 1, 4, f) == 4;
    }
    return finish_write(f, path, ok, err);
}
