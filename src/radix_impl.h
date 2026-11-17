/* Key-dependent half of the radix engine, instantiated by sort.c once per key kind:
 * NS_FN(name) adds the kind suffix, NS_KEY(x, km) maps an element to its radix key.
 * SPDX-License-Identifier: MIT */

static unsigned NS_FN(digit)(uint32_t x, unsigned pass, ns_keymap km) {
    return (NS_KEY(x, km) >> (pass * 8u)) & 0xFFu;
}

/* ── Small inputs ──────────────────────────────────────────────────────── */
static void NS_FN(insertion_sort)(uint32_t *a, size_t n, ns_keymap km) {
    for (size_t i = 1; i < n; i++) {
        uint32_t x = a[i];
        uint32_t key = NS_KEY(x, km);
        size_t j = i;
        while (j > 0 && NS_KEY(a[j - 1], km) > key) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = x;
    }
}

static int NS_FN(is_ascending)(const uint32_t *a, size_t n, ns_keymap km) {
    for (size_t i = 1; i < n; i++)
        if (NS_KEY(a[i - 1], km) > NS_KEY(a[i], km)) return 0;
    return 1;
}

static int NS_FN(is_descending)(const uint32_t *a, size_t n, ns_keymap km) {
    for (size_t i = 1; i < n; i++)
        if (NS_KEY(a[i - 1], km) <= NS_KEY(a[i], km)) return 0;
    return 1;
}

static void NS_FN(histogram_all)(const uint32_t *a, size_t n, ns_keymap km, ns_hist_all h) {
    memset(h, 0, sizeof(ns_hist_all));
    for (size_t i = 0; i < n; i++) {
        uint32_t k = NS_KEY(a[i], km);
        h[0][k & 0xFFu]++;
        h[1][(k >> 8) & 0xFFu]++;
        h[2][(k >> 16) & 0xFFu]++;
        h[3][k >> 24]++;
    }
}

static void NS_FN(scatter)(const uint32_t *src, size_t n, uint32_t *dst, size_t *offset,
                           unsigned pass, ns_keymap km) {
    for (size_t i = 0; i < n; i++) {
        uint32_t x = src[i];
        dst[offset[NS_FN(digit)(x, pass, km)]++] = x;
    }
}

/* ── Sequential radix ──────────────────────────────────────────────────── */
static void NS_FN(radix_seq)(uint32_t *data, uint32_t *buf, size_t n, ns_keymap km) {
    ns_hist_all hist;
    NS_FN(histogram_all)(data, n, km, hist);

    uint32_t *src = data, *dst = buf;
    for (unsigned p = 0; p < NS_PASSES; p++) {
        if (pass_is_trivial(hist[p], n)) continue;
        size_t offset[NS_BUCKETS], running = 0;
        for (unsigned d = 0; d < NS_BUCKETS; d++) {
            offset[d] = running;
            running += hist[p][d];
        }
        NS_FN(scatter)(src, n, dst, offset, p, km);
        uint32_t *tmp = src;
        src = dst;
        dst = tmp;
    }
    if (src != data) memcpy(data, src, n * sizeof *data);
}

static void NS_FN(worker_run)(ns_ctx *c, unsigned id) {
    size_t begin, end;
    chunk_bounds(c->n, c->nthreads, id, &begin, &end);
    size_t len = end - begin;

    NS_FN(histogram_all)(c->data + begin, len, c->km, c->hist0[id]);
    ns_barrier_wait(&c->barrier);
    if (id == 0) plan_passes(c);
    ns_barrier_wait(&c->barrier);

    uint32_t *src = c->data, *dst = c->buf;
    for (unsigned k = 0; k < c->nactive; k++) {
        unsigned p = c->active[k];
        size_t *count = c->count[id];
        if (k == 0) {
            memcpy(count, c->hist0[id][p], sizeof(ns_hist));
        } else {
            memset(count, 0, sizeof(ns_hist));
            for (size_t i = begin; i < end; i++) count[NS_FN(digit)(src[i], p, c->km)]++;
        }
        ns_barrier_wait(&c->barrier);
        if (id == 0) compute_offsets(c);
        ns_barrier_wait(&c->barrier);

        size_t offset[NS_BUCKETS];
        memcpy(offset, c->offset[id], sizeof offset);
        NS_FN(scatter)(src + begin, len, dst, offset, p, c->km);
        ns_barrier_wait(&c->barrier);

        uint32_t *tmp = src;
        src = dst;
        dst = tmp;
    }
    if (src != c->data) memcpy(c->data + begin, src + begin, len * sizeof *src);
}

/* ── Strategy choice ───────────────────────────────────────────────────── */
static nodesort_status NS_FN(sort)(uint32_t *data, size_t n, ns_keymap km, unsigned threads,
                                   size_t seq_threshold) {
    if (n < 2) return NODESORT_OK;
    if (n <= NS_INSERTION_MAX) {
        NS_FN(insertion_sort)(data, n, km);
        return NODESORT_OK;
    }
    if (NS_FN(is_ascending)(data, n, km)) return NODESORT_OK;
    if (NS_FN(is_descending)(data, n, km)) {
        reverse(data, n);
        return NODESORT_OK;
    }

    size_t max_by_size = n / NS_MIN_PER_THREAD;
    size_t nthreads = threads;
    if (nthreads > NS_MAX_THREADS) nthreads = NS_MAX_THREADS;
    if (nthreads > max_by_size) nthreads = max_by_size;
    if (nthreads < 1 || n < seq_threshold) nthreads = 1;

    uint32_t *buf = (uint32_t *)ns_alloc(n, sizeof *buf);
    if (!buf) return NODESORT_ENOMEM;

    nodesort_status status = NODESORT_OK;
    if (nthreads == 1)
        NS_FN(radix_seq)(data, buf, n, km);
    else
        status = radix_par(data, buf, n, km, (unsigned)nthreads, NS_FN(worker_run));

    free(buf);
    return status;
}
