/* Fixture generator and result checker for the `nodesort` CLI end-to-end tests.
 *   cli_check gen DIR
 *   cli_check check-sort INPUT OUTPUT asc|desc
 *   cli_check check-top MATRIX QUERY RESULT cosine|dot|l2 K [IDS]
 * Exit status 0 = ok.
 * SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array_io.h"
#include "nodesort/nodesort.h"

static int failf(const char *msg, const char *detail) {
    fprintf(stderr, "cli_check: %s%s%s\n", msg, detail ? ": " : "", detail ? detail : "");
    return 1;
}

static uint64_t rng_next(uint64_t *s) {
    uint64_t z = (*s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static double unit(uint64_t *s) { return (double)(int64_t)rng_next(s) / 9.2233720368547758e18; }

static int save(const char *dir, const char *name, array_t *a, int npy) {
    char path[1024], err[ERR_LEN];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (array_write(path, a, npy, err) != 0) return failf("write failed", err);
    return 0;
}

static int save_bytes(const char *dir, const char *name, const void *p, size_t n) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return failf("cannot create", path);
    size_t w = fwrite(p, 1, n, f);
    fclose(f);
    return w == n ? 0 : failf("write failed", path);
}

/* ── gen ───────────────────────────────────────────────────────────────── */
static int gen(const char *dir) {
    enum { N = 5000, ROWS = 3000, D = 16 };
    uint64_t s = 42;
    int rc = 0;

    float f32[N];
    double f64[N];
    int32_t i32[N];
    uint32_t u32[N];
    for (size_t i = 0; i < N; i++) {
        f32[i] = (float)unit(&s);
        f64[i] = unit(&s);
        i32[i] = (int32_t)(rng_next(&s) >> 32);
        u32[i] = (uint32_t)(rng_next(&s) % 100u); /* many duplicates */
    }
    f32[7] = NAN;
    f32[8] = -0.0f;
    f32[9] = 0.0f;
    f32[10] = INFINITY;
    f64[3] = NAN;
    f64[4] = -INFINITY;

    array_t a = array_view(DT_F32, N, 0, f32, 1);
    rc |= save(dir, "s_f32.npy", &a, 1);
    rc |= save(dir, "s_f32_inplace.npy", &a, 1);
    rc |= save(dir, "s_raw.f32", &a, 0);
    a.type = DT_F64;
    a.data = f64;
    rc |= save(dir, "s_f64.npy", &a, 1);
    a.type = DT_I32;
    a.data = i32;
    rc |= save(dir, "s_i32.npy", &a, 1);
    a.type = DT_U32;
    a.data = u32;
    rc |= save(dir, "s_u32.npy", &a, 1);

    /* Embeddings: random rows, a zero row (cosine NaN) and duplicated rows (ties). */
    static float m[ROWS * D];
    for (size_t i = 0; i < (size_t)ROWS * D; i++) m[i] = (float)unit(&s);
    memset(m + 17 * D, 0, D * sizeof(float));
    memcpy(m + 100 * D, m + 50 * D, D * sizeof(float));
    memcpy(m + 2000 * D, m + 50 * D, D * sizeof(float));
    float q[D];
    for (size_t c = 0; c < D; c++) q[c] = m[50 * D + c] + 0.01f * (float)unit(&s);
    array_t mat = array_view(DT_F32, ROWS, D, m, 1);
    rc |= save(dir, "emb.npy", &mat, 1);
    rc |= save(dir, "emb.f32", &mat, 0);
    array_t qa = array_view(DT_F32, D, 0, q, 1);
    rc |= save(dir, "q.npy", &qa, 1);
    rc |= save(dir, "q.f32", &qa, 0);
    float qz[D] = {0};
    qa.data = qz;
    rc |= save(dir, "q_zero.npy", &qa, 1);
    qa = array_view(DT_F32, 5, 0, q, 1);
    rc |= save(dir, "q_short.npy", &qa, 1);

    static uint64_t ids[ROWS];
    for (size_t r = 0; r < ROWS; r++) ids[r] = (UINT64_C(1) << 40) + r * 7u;
    array_t ia = array_view(DT_U64, ROWS, 0, ids, 1);
    rc |= save(dir, "ids.npy", &ia, 1);

    /* Invalid files: a v1.0 header padded to 128 bytes, then 16 payload bytes. */
    const char *bad[][2] = {
        {"bad_f16.npy", "{'descr': '<f2', 'fortran_order': False, 'shape': (2,), }"},
        {"bad_be.npy", "{'descr': '>f4', 'fortran_order': False, 'shape': (2,), }"},
        {"bad_3d.npy", "{'descr': '<f4', 'fortran_order': False, 'shape': (2, 2, 2), }"},
        {"bad_trunc.npy", "{'descr': '<f4', 'fortran_order': False, 'shape': (8,), }"}};
    for (size_t b = 0; b < sizeof bad / sizeof bad[0]; b++) {
        unsigned char buf[144] = {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0, 118, 0};
        size_t len = strlen(bad[b][1]);
        memcpy(buf + 10, bad[b][1], len);
        memset(buf + 10 + len, ' ', 117 - len);
        buf[127] = '\n';
        rc |= save_bytes(dir, bad[b][0], buf, sizeof buf); /* bad_trunc: 16 of 32 bytes */
    }
    return rc;
}

/* ── check-sort ────────────────────────────────────────────────────────── */
static int check_sort(const char *in, const char *out, int descending) {
    char err[ERR_LEN];
    array_t a, b;
    if (array_read(in, DT_NONE, 0, &a, err) != 0) return failf("read input", err);
    if (array_read(out, DT_NONE, 0, &b, err) != 0) return failf("read output", err);
    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.descending = descending;
    nodesort_status st;
    switch (a.type) {
    case DT_F32: st = nodesort_f32((float *)a.data, a.count, &opt); break;
    case DT_F64: st = nodesort_f64((double *)a.data, a.count, &opt); break;
    case DT_I32: st = nodesort_i32((int32_t *)a.data, a.count, &opt); break;
    default: st = nodesort_u32((uint32_t *)a.data, a.count, &opt); break;
    }
    int ok = st == NODESORT_OK && a.type == b.type && a.count == b.count &&
             a.from_npy == b.from_npy && memcmp(a.data, b.data, a.count * dtype_size(a.type)) == 0;
    array_free(&a);
    array_free(&b);
    return ok ? 0 : failf("sorted output differs from the library result", out);
}

/* ── check-top: independent brute force ────────────────────────────────── */
static int check_top(const char *mpath, const char *qpath, const char *rpath, const char *metric,
                     size_t k, const char *ids_path) {
    char err[ERR_LEN];
    array_t m, q, ids;
    memset(&ids, 0, sizeof ids);
    if (array_read(mpath, DT_NONE, 16, &m, err) != 0) return failf("read matrix", err);
    if (array_read(qpath, DT_NONE, 0, &q, err) != 0) return failf("read query", err);
    if (ids_path && array_read(ids_path, DT_NONE, 0, &ids, err) != 0) return failf("ids", err);
    int l2 = strcmp(metric, "l2") == 0, cosine = strcmp(metric, "cosine") == 0;

    const float *mf = (const float *)m.data, *qf = (const float *)q.data;
    double qq = 0;
    for (size_t c = 0; c < m.cols; c++) qq += (double)qf[c] * (double)qf[c];
    float *score = (float *)malloc(m.rows * sizeof *score);
    size_t *order = (size_t *)malloc(m.rows * sizeof *order);
    for (size_t r = 0; r < m.rows; r++) {
        double acc = 0, xx = 0;
        for (size_t c = 0; c < m.cols; c++) {
            double x = (double)mf[r * m.cols + c], y = (double)qf[c];
            if (l2)
                acc += (x - y) * (x - y);
            else
                acc += x * y;
            xx += x * x;
        }
        if (l2)
            acc = sqrt(acc);
        else if (cosine)
            acc = xx > 0 ? acc / (sqrt(xx) * sqrt(qq)) : (double)NAN;
        score[r] = (float)acc;
        order[r] = r;
    }
    /* Stable selection of the k best: NaN last, then by score, ties by row. */
    for (size_t i = 0; i < k && i < m.rows; i++) {
        size_t best = i;
        for (size_t j = i + 1; j < m.rows; j++) {
            float a = score[order[j]], b = score[order[best]];
            int a_nan = a != a, b_nan = b != b;
            int better = !a_nan && (b_nan || (l2 ? a < b : a > b));
            if (better || (!a_nan && !b_nan && a == b && order[j] < order[best])) best = j;
        }
        size_t t = order[i];
        order[i] = order[best];
        order[best] = t;
    }

    FILE *f = fopen(rpath, "rb");
    if (!f) return failf("cannot open result", rpath);
    unsigned char hdr[10];
    size_t got = fread(hdr, 1, 10, f);
    size_t hlen = got == 10 ? (size_t)hdr[8] | ((size_t)hdr[9] << 8) : 0;
    char *h = (char *)calloc(1, hlen + 1);
    if (!h || fread(h, 1, hlen, f) != hlen) return failf("bad result header", rpath);
    size_t idsize = strstr(h, "'<u8'") ? 8 : 4;
    size_t expect_k = k < m.rows ? k : m.rows;
    int ok = strstr(h, "('score', '<f4')") != NULL;
    for (size_t i = 0; ok && i < expect_k; i++) {
        uint64_t id = 0;
        float sc;
        if (fread(&id, 1, idsize, f) != idsize || fread(&sc, 1, 4, f) != 4) {
            ok = 0;
            break;
        }
        uint64_t want_id = ids_path ? ((const uint64_t *)ids.data)[order[i]] : order[i];
        float want = score[order[i]];
        /* Scores may differ in the last bits if a compiler fuses multiply-adds differently. */
        float tol = 1e-5f * (fabsf(want) > 1.0f ? fabsf(want) : 1.0f);
        int same = (sc != sc && want != want) || fabsf(sc - want) <= tol;
        ok = id == want_id && same;
        if (!ok)
            fprintf(stderr, "rank %zu: got id %llu score %.9g, want id %llu score %.9g\n", i + 1,
                    (unsigned long long)id, (double)sc, (unsigned long long)want_id, (double)want);
    }
    ok = ok && fgetc(f) == EOF;
    fclose(f);
    free(h);
    free(score);
    free(order);
    array_free(&m);
    array_free(&q);
    array_free(&ids);
    return ok ? 0 : failf("top-k result differs from brute force", rpath);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "gen") == 0) return gen(argv[2]);
    if (argc == 5 && strcmp(argv[1], "check-sort") == 0)
        return check_sort(argv[2], argv[3], strcmp(argv[4], "desc") == 0);
    if ((argc == 7 || argc == 8) && strcmp(argv[1], "check-top") == 0)
        return check_top(argv[2], argv[3], argv[4], argv[5], (size_t)strtoul(argv[6], NULL, 10),
                         argc == 8 ? argv[7] : NULL);
    return failf("usage: gen DIR | check-sort IN OUT asc|desc | check-top M Q R METRIC K [IDS]",
                 NULL);
}
