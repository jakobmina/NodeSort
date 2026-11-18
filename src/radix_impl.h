/* Type-dependent half of the radix engine, instantiated by sort.c once per (key, value) kind.
 * Parameters:
 *   NS_FN(name)  adds the instantiation suffix
 *   NS_K         key element type (uint32_t or uint64_t), NS_KPASSES its 8-bit passes (4 or 8)
 *   NS_KEY(x,km) maps a key element to its radix key
 *   NS_HAS_V     1 when a parallel value array of NS_V elements moves with the keys
 * SPDX-License-Identifier: MIT */

#if NS_HAS_V
#  define NS_VPARAM(name) , NS_V *name
#  define NS_VCPARAM(name) , const NS_V *name
#  define NS_VARG(expr) , (expr)
#  define NS_VDO(stmt) stmt
#else
#  define NS_VPARAM(name)
#  define NS_VCPARAM(name)
#  define NS_VARG(expr)
#  define NS_VDO(stmt)
#endif

static unsigned NS_FN(digit)(NS_K x, unsigned pass, ns_keymap km) {
    return (unsigned)((NS_KEY(x, km) >> (pass * 8u)) & 0xFFu);
}

/* ── Small and already-ordered inputs ──────────────────────────────────── */
static void NS_FN(insertion_sort)(NS_K *a NS_VPARAM(v), size_t n, ns_keymap km) {
    for (size_t i = 1; i < n; i++) {
        NS_K x = a[i];
        NS_VDO(NS_V vx = v[i];)
        NS_K key = NS_KEY(x, km);
        size_t j = i;
        while (j > 0 && NS_KEY(a[j - 1], km) > key) {
            a[j] = a[j - 1];
            NS_VDO(v[j] = v[j - 1];)
            j--;
        }
        a[j] = x;
        NS_VDO(v[j] = vx;)
    }
}

static int NS_FN(is_ascending)(const NS_K *a, size_t n, ns_keymap km) {
    for (size_t i = 1; i < n; i++)
        if (NS_KEY(a[i - 1], km) > NS_KEY(a[i], km)) return 0;
    return 1;
}

static int NS_FN(is_strictly_descending)(const NS_K *a, size_t n, ns_keymap km) {
    for (size_t i = 1; i < n; i++)
        if (NS_KEY(a[i - 1], km) <= NS_KEY(a[i], km)) return 0;
    return 1;
}

static void NS_FN(reverse)(NS_K *a NS_VPARAM(v), size_t n) {
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        NS_K t = a[i];
        a[i] = a[j];
        a[j] = t;
        NS_VDO(NS_V tv = v[i]; v[i] = v[j]; v[j] = tv;)
    }
}

/* ── Radix building blocks ─────────────────────────────────────────────── */
static void NS_FN(histogram_all)(const NS_K *a, size_t n, ns_keymap km, ns_hist_all h) {
    memset(h, 0, sizeof(ns_hist) * NS_KPASSES);
    for (size_t i = 0; i < n; i++) {
        NS_K k = NS_KEY(a[i], km);
        for (unsigned p = 0; p < NS_KPASSES; p++) h[p][(k >> (p * 8u)) & 0xFFu]++;
    }
}

static void NS_FN(scatter)(const NS_K *src NS_VCPARAM(srcv), size_t n, NS_K *dst NS_VPARAM(dstv),
                           size_t *offset, unsigned pass, ns_keymap km) {
    for (size_t i = 0; i < n; i++) {
        NS_K x = src[i];
        size_t pos = offset[NS_FN(digit)(x, pass, km)]++;
        dst[pos] = x;
        NS_VDO(dstv[pos] = srcv[i];)
    }
}

/* ── Sequential radix ──────────────────────────────────────────────────── */
static void NS_FN(radix_seq)(NS_K *keys NS_VPARAM(vals), NS_K *kbuf NS_VPARAM(vbuf), size_t n,
                             ns_keymap km) {
    ns_hist_all hist;
    NS_FN(histogram_all)(keys, n, km, hist);

    NS_K *src = keys, *dst = kbuf;
    NS_VDO(NS_V *srcv = vals; NS_V *dstv = vbuf;)
    for (unsigned p = 0; p < NS_KPASSES; p++) {
        if (pass_is_trivial(hist[p], n)) continue;
        size_t offset[NS_BUCKETS], running = 0;
        for (unsigned d = 0; d < NS_BUCKETS; d++) {
            offset[d] = running;
            running += hist[p][d];
        }
        NS_FN(scatter)(src NS_VARG(srcv), n, dst NS_VARG(dstv), offset, p, km);
        NS_K *tmp = src;
        src = dst;
        dst = tmp;
        NS_VDO(NS_V *tmpv = srcv; srcv = dstv; dstv = tmpv;)
    }
    if (src != keys) {
        memcpy(keys, src, n * sizeof *keys);
        NS_VDO(memcpy(vals, srcv, n * sizeof *vals);)
    }
}

/* ── Parallel radix worker (see radix_par in sort.c) ───────────────────── */
static void NS_FN(worker_run)(ns_ctx *c, unsigned id) {
    size_t begin, end;
    chunk_bounds(c->n, c->nthreads, id, &begin, &end);
    size_t len = end - begin;
    NS_K *keys = (NS_K *)c->keys;

    NS_FN(histogram_all)(keys + begin, len, c->km, c->hist0[id]);
    ns_barrier_wait(&c->barrier);
    if (id == 0) plan_passes(c);
    ns_barrier_wait(&c->barrier);

    NS_K *src = keys, *dst = (NS_K *)c->kbuf;
    NS_VDO(NS_V *srcv = (NS_V *)c->vals; NS_V *dstv = (NS_V *)c->vbuf;)
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
        NS_FN(scatter)(src + begin NS_VARG(srcv + begin), len, dst NS_VARG(dstv), offset, p, c->km);
        ns_barrier_wait(&c->barrier);

        NS_K *tmp = src;
        src = dst;
        dst = tmp;
        NS_VDO(NS_V *tmpv = srcv; srcv = dstv; dstv = tmpv;)
    }
    if (src != keys) {
        memcpy(keys + begin, src + begin, len * sizeof *src);
        NS_VDO(memcpy((NS_V *)c->vals + begin, srcv + begin, len * sizeof *srcv);)
    }
}

/* ── Strategy choice ───────────────────────────────────────────────────── */
static nodesort_status NS_FN(sort)(NS_K *keys NS_VPARAM(vals), size_t n, ns_keymap km,
                                   unsigned threads, size_t seq_threshold) {
    if (n < 2) return NODESORT_OK;
    if (n <= NS_INSERTION_MAX) {
        NS_FN(insertion_sort)(keys NS_VARG(vals), n, km);
        return NODESORT_OK;
    }
    if (NS_FN(is_ascending)(keys, n, km)) return NODESORT_OK;
    if (NS_FN(is_strictly_descending)(keys, n, km)) {
        NS_FN(reverse)(keys NS_VARG(vals), n);
        return NODESORT_OK;
    }

    unsigned nthreads = effective_threads(n, threads, seq_threshold);

    NS_K *kbuf = (NS_K *)ns_alloc(n, sizeof *kbuf);
    if (!kbuf) return NODESORT_ENOMEM;
#if NS_HAS_V
    NS_V *vbuf = (NS_V *)ns_alloc(n, sizeof *vbuf);
    if (!vbuf) {
        free(kbuf);
        return NODESORT_ENOMEM;
    }
#endif

    nodesort_status status = NODESORT_OK;
    if (nthreads == 1) {
        NS_FN(radix_seq)(keys NS_VARG(vals), kbuf NS_VARG(vbuf), n, km);
    } else {
        ns_ctx c;
        memset(&c, 0, sizeof c);
        c.keys = keys;
        c.kbuf = kbuf;
        NS_VDO(c.vals = vals; c.vbuf = vbuf;)
        c.n = n;
        c.km = km;
        c.passes = NS_KPASSES;
        c.nthreads = nthreads;
        c.run = NS_FN(worker_run);
        status = radix_par(&c);
    }

    NS_VDO(free(vbuf);)
    free(kbuf);
    return status;
}

#undef NS_VPARAM
#undef NS_VCPARAM
#undef NS_VARG
#undef NS_VDO
