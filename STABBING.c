#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include "generator.h"

#define MC_VERSION MC_NEWEST
#define START_SEED 0LL
#define THREADS 18
#define LB 0
#define Y_LEVEL 256
#define SY (Y_LEVEL >> 2)
#define TILE 64
#define TBITS 6
#define TMASK 63
#define TAREA (TILE * TILE)
#define MAX_CELLS 300000000LL
#define RING (1 << 16)
#define STATUS_INTERVAL 20
#define CKPT_INTERVAL 30
#define RESULTS_FILE "stabbing%s_results.txt"
#define CHECKPOINT_FILE "stabbing%s_checkpoint.txt"

static const char *BIOME_NAMES[256] = {
    /* surface biomes, sampled at y=256 */
    [ocean] = "Ocean",
    [plains] = "Plains",
    [desert] = "Desert",
    [windswept_hills] = "Windswept Hills",
    [forest] = "Forest",
    [taiga] = "Taiga",
    [swamp] = "Swamp",
    [river] = "River",
    [frozen_ocean] = "Frozen Ocean",
    [frozen_river] = "Frozen River",
    [snowy_plains] = "Snowy Plains",
    [mushroom_fields] = "Mushroom Fields",
    [beach] = "Beach",
    [jungle] = "Jungle",
    [sparse_jungle] = "Sparse Jungle",
    [deep_ocean] = "Deep Ocean",
    [stony_shore] = "Stony Shore",
    [snowy_beach] = "Snowy Beach",
    [birch_forest] = "Birch Forest",
    [dark_forest] = "Dark Forest",
    [snowy_taiga] = "Snowy Taiga",
    [old_growth_pine_taiga] = "Old Growth Pine Taiga",
    [windswept_forest] = "Windswept Forest",
    [savanna] = "Savanna",
    [savanna_plateau] = "Savanna Plateau",
    [badlands] = "Badlands",
    [wooded_badlands] = "Wooded Badlands",
    [warm_ocean] = "Warm Ocean",
    [lukewarm_ocean] = "Lukewarm Ocean",
    [cold_ocean] = "Cold Ocean",
    [deep_lukewarm_ocean] = "Deep Lukewarm Ocean",
    [deep_cold_ocean] = "Deep Cold Ocean",
    [deep_frozen_ocean] = "Deep Frozen Ocean",
    [sunflower_plains] = "Sunflower Plains",
    [windswept_gravelly_hills] = "Windswept Gravelly Hills",
    [flower_forest] = "Flower Forest",
    [ice_spikes] = "Ice Spikes",
    [old_growth_birch_forest] = "Old Growth Birch Forest",
    [old_growth_spruce_taiga] = "Old Growth Spruce Taiga",
    [windswept_savanna] = "Windswept Savanna",
    [eroded_badlands] = "Eroded Badlands",
    [bamboo_jungle] = "Bamboo Jungle",
    [meadow] = "Meadow",
    [grove] = "Grove",
    [snowy_slopes] = "Snowy Slopes",
    [jagged_peaks] = "Jagged Peaks",
    [frozen_peaks] = "Frozen Peaks",
    [stony_peaks] = "Stony Peaks",
    [mangrove_swamp] = "Mangrove Swamp",
    [cherry_grove] = "Cherry Grove",
    [pale_garden] = "Pale Garden",
    [dappled_forest] = "Dappled Forest",

    /* cave biomes, sampled at a specified depth */
    [dripstone_caves] = "Dripstone Caves",
    [lush_caves] = "Lush Caves",
    [deep_dark] = "Deep Dark",
    [sulfur_caves] = "Sulfur Caves",
};

/* biomes that bridge across rivers */
static const uint8_t BRIDGE[256] = {
    [plains] = 1,
    [desert] = 1,
    [forest] = 1,
    [dark_forest] = 1,
    [taiga] = 1,
    [snowy_taiga] = 1,
    [snowy_plains] = 1,
    [savanna] = 1,
    [badlands] = 1,
    [wooded_badlands] = 1,
    [beach] = 1,
    [snowy_beach] = 1,
    [stony_shore] = 1,
    [grove] = 1,
    [snowy_slopes] = 1,
};

static inline int64_t tkey(int tx, int tz){ return ((int64_t)(uint32_t)tx << 32) | (uint32_t)tz; }
static inline uint64_t mix(uint64_t x){ x ^= x>>30; x *= 0xbf58476d1ce4e5b9ULL; x ^= x>>27; x *= 0x94d049bb133111ebULL; x ^= x>>31; return x; }

typedef struct { int tx, tz; uint8_t *vis; } Tile;

/* each thread keeps one of these and reuses it for every seed */
typedef struct
{
    Generator g;
    Tile *tiles;
    size_t ntiles, nvis, tcap;
    int64_t *hkey;
    int *hidx;
    uint32_t *hgen, gen;
    size_t hcap;
    int *stx, *stz;
    size_t scap, sn;
    int cache_tx, cache_tz, cache_idx;
} Ctx;

static void hgrow(Ctx *c)
{
    size_t nc = c->hcap ? c->hcap << 1 : (1 << 12), m = nc - 1;
    int64_t *nk = malloc(nc * sizeof(int64_t));
    int *ni = malloc(nc * sizeof(int));
    uint32_t *ng = calloc(nc, sizeof(uint32_t));
    for (size_t i = 0; i < c->hcap; i++)
    {
        if (c->hgen[i] != c->gen) continue;
        size_t j = mix((uint64_t)c->hkey[i]) & m;
        while (ng[j] == c->gen) j = (j + 1) & m;
        nk[j] = c->hkey[i]; ni[j] = c->hidx[i]; ng[j] = c->gen;
    }
    free(c->hkey); free(c->hidx); free(c->hgen); c->hkey = nk; c->hidx = ni; c->hgen = ng; c->hcap = nc;
}

static int tile_lookup(Ctx *c, int tx, int tz)
{
    int64_t k = tkey(tx, tz);
    size_t m = c->hcap - 1, i = mix((uint64_t)k) & m;
    while (c->hgen[i] == c->gen) { if (c->hkey[i] == k) return c->hidx[i]; i = (i + 1) & m; }
    return -1;
}

static int tile_new(Ctx *c, int tx, int tz)
{
    if ((c->ntiles + 1) * 10 >= c->hcap * 7) hgrow(c);
    if (c->ntiles == c->tcap) { c->tcap = c->tcap ? c->tcap * 2 : 1024; c->tiles = realloc(c->tiles, c->tcap * sizeof(Tile)); }
    int idx = (int)c->ntiles++;
    Tile *t = &c->tiles[idx];
    t->tx = tx; t->tz = tz;
    if (idx < (int)c->nvis) memset(t->vis, 0, TAREA);
    else { t->vis = calloc(TAREA, 1); c->nvis++; }
    int64_t k = tkey(tx, tz);
    size_t m = c->hcap - 1, i = mix((uint64_t)k) & m;
    while (c->hgen[i] == c->gen) i = (i + 1) & m;
    c->hkey[i] = k; c->hidx[i] = idx; c->hgen[i] = c->gen;
    return idx;
}

static int tile_slot(Ctx *c, int tx, int tz)
{
    if (tx == c->cache_tx && tz == c->cache_tz) return c->cache_idx;
    int idx = tile_lookup(c, tx, tz);
    if (idx < 0) idx = tile_new(c, tx, tz);
    c->cache_tx = tx; c->cache_tz = tz; c->cache_idx = idx;
    return idx;
}

static void spush(Ctx *c, int x, int z)
{
    if (c->sn == c->scap) { c->scap = c->scap ? c->scap * 2 : (1 << 16); c->stx = realloc(c->stx, c->scap * sizeof(int)); c->stz = realloc(c->stz, c->scap * sizeof(int)); }
    c->stx[c->sn] = x; c->stz[c->sn] = z; c->sn++;
}

/* rivers generate where weirdness is within 0.05 of 0, this decides what would generate if the river didn't exist */
static int under_river(Generator *g, int64_t *np)
{
    if (np[5] > 500 || np[5] < -500) return -1;
    np[5] = np[5] >= 0 ? 501 : -501;
    return climateToBiome(g->mc, (const uint64_t *)np, NULL);
}

static Ctx *ctx_new(void)
{
    Ctx *c = calloc(1, sizeof *c);
    setupGenerator(&c->g, MC_VERSION, LB ? LARGE_BIOMES : 0);
    return c;
}

/* measures the biome touching 0 0, returns its 1:4 size (or -1 if it hits MAX_CELLS) */
static long long measure(Ctx *c, uint64_t seed, int *biome)
{
    Generator *g = &c->g;
    applySeed(g, DIM_OVERWORLD, seed);
    int scx = 0, scz = 0;

    /* determine the biome at 0 0 then step to the nearest 1:4 cell of it. */
    int target = getBiomeAt(g, 1, 0, Y_LEVEL, 0);
    if (getBiomeAt(g, 4, scx, SY, scz) != target)
    {
        int found = 0;
        for (int r = 1; r <= 24 && !found; r++)
        for (int dx = -r; dx <= r && !found; dx++)
        for (int dz = -r; dz <= r && !found; dz++)
        {
            if (abs(dx) != r && abs(dz) != r) continue;
            if (getBiomeAt(g, 4, scx + dx, SY, scz + dz) == target) { scx += dx; scz += dz; found = 1; }
        }
        if (!found) target = getBiomeAt(g, 4, scx, SY, scz);
    }
    *biome = target;

    int bridge = target >= 0 && target < 256 && BRIDGE[target];
    /* grove/snowy slopes generate these instead of rivers */
    int b0 = river, b1 = frozen_river;
    if (target == grove) { b0 = snowy_taiga; b1 = taiga; }
    else if (target == snowy_slopes) { b0 = snowy_plains; b1 = ice_spikes; }

    if (++c->gen == 0) { memset(c->hgen, 0, c->hcap * sizeof(uint32_t)); c->gen = 1; }
    c->ntiles = 0; c->sn = 0;
    c->cache_tx = scx >> TBITS; c->cache_tz = scz >> TBITS;
    c->cache_idx = tile_new(c, c->cache_tx, c->cache_tz);
    c->tiles[c->cache_idx].vis[(scz & TMASK) * TILE + (scx & TMASK)] = 1;
    spush(c, scx, scz);

    long long count = 0;
    int dxu[4] = {0, 0, -1, 1}, dzu[4] = {-1, 1, 0, 0};
    int64_t np[6];
    while (c->sn)
    {
        c->sn--;
        int x = c->stx[c->sn], z = c->stz[c->sn];
        int bm = sampleBiomeNoise(&g->bn, np, x, SY, z, NULL, 0);
        if (bm == target) { if (++count >= MAX_CELLS) return -1; }
        else if (!bridge || (bm != river && bm != frozen_river && bm != b0 && bm != b1) || under_river(g, np) != target) continue;
        for (int i = 0; i < 4; i++)
        {
            int nx = x + dxu[i], nz = z + dzu[i];
            int nti = tile_slot(c, nx >> TBITS, nz >> TBITS);
            Tile *nt = &c->tiles[nti];
            int nloc = (nz & TMASK) * TILE + (nx & TMASK);
            if (nt->vis[nloc]) continue;
            nt->vis[nloc] = 1;
            spush(c, nx, nz);
        }
    }
    return count;
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

/* finished seeds wait here until every seed before them is done, so the file stays in order */
static long long rsize[RING];
static int16_t rbiome[RING];
static atomic_char rdone[RING];
static atomic_llong g_next, g_head;
static long long g_start, g_best = -1, g_bestseed;
static int g_bestbiome;
static pthread_mutex_t g_outmtx = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_out;
static char g_results[64], g_ckpt[64];

static const char *biome_name(int id)
{
    return id >= 0 && id < 256 && BIOME_NAMES[id] ? BIOME_NAMES[id] : "Unknown";
}

static void flush_results(void)
{
    pthread_mutex_lock(&g_outmtx);
    long long h = atomic_load(&g_head);
    size_t i;
    while (atomic_load(&rdone[i = (uint64_t)h & (RING - 1)]))
    {
        long long n = rsize[i];
        if (n < 0) fprintf(stderr, "%lld: patch exceeds %lld cells\n", h, MAX_CELLS);
        fprintf(g_out, "%lld ", h);
        for (const char *nm = biome_name(rbiome[i]); *nm; nm++) fputc(*nm == ' ' ? '_' : *nm, g_out);
        fprintf(g_out, " %lld %lld\n", n, n < 0 ? n : n * 16);
        if (n > g_best) { g_best = n; g_bestseed = h; g_bestbiome = rbiome[i]; }
        atomic_store(&rdone[i], 0);
        h++;
    }
    atomic_store(&g_head, h);
    pthread_mutex_unlock(&g_outmtx);
}

static void *worker(void *a)
{
    (void)a;
    Ctx *c = ctx_new();
    while (!g_stop)
    {
        long long s = atomic_fetch_add(&g_next, 1);
        while (s - atomic_load(&g_head) >= RING && !g_stop) usleep(1000);
        if (g_stop) break;
        int bm;
        long long n = measure(c, (uint64_t)s, &bm);
        size_t i = (uint64_t)s & (RING - 1);
        rsize[i] = n; rbiome[i] = (int16_t)bm;
        atomic_store(&rdone[i], 1);
        flush_results();
    }
    return NULL;
}

static void write_checkpoint(void)
{
    pthread_mutex_lock(&g_outmtx);
    fflush(g_out);
    FILE *f = fopen(g_ckpt, "w");
    if (f) { fprintf(f, "%lld\n", atomic_load(&g_head)); fclose(f); }
    pthread_mutex_unlock(&g_outmtx);
}

static void *ckpt(void *a)
{
    (void)a;
    while (!g_stop) { sleep(CKPT_INTERVAL); write_checkpoint(); }
    return NULL;
}

static inline double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static void *statmon(void *a)
{
    (void)a;
    double t0 = now_s(), tl = t0;
    long long last = 0;
    while (!g_stop)
    {
        sleep(STATUS_INTERVAL);
        double tn = now_s(), dt = tn - tl; if (dt <= 0) dt = 1;
        pthread_mutex_lock(&g_outmtx);
        long long seeds = atomic_load(&g_head) - g_start, best = g_best, bs = g_bestseed;
        int bb = g_bestbiome;
        pthread_mutex_unlock(&g_outmtx);
        double el = tn - t0;
        char ts[16];
        if (el < 60) snprintf(ts, sizeof ts, "%.0fs", el);
        else if (el < 3600) snprintf(ts, sizeof ts, "%.1fm", el / 60);
        else snprintf(ts, sizeof ts, "%.1fh", el / 3600);
        fprintf(stderr, "[%s] %.2fM  %.0f/s | Current Seed: %lld | Best=%s %lld %lld\n",
            ts, seeds / 1e6, (seeds - last) / dt, g_start + seeds, biome_name(bb), bs, best);
        last = seeds; tl = tn;
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc >= 3 && !strcmp(argv[1], "single"))
    {
        Ctx *c = ctx_new();
        int bm;
        long long n = measure(c, (uint64_t)strtoll(argv[2], NULL, 10), &bm);
        if (n < 0)
        {
            fprintf(stderr, "patch exceeds %lld cells; check Y_LEVEL\n", MAX_CELLS);
            return 1;
        }
        char ys[32] = "";
        if (Y_LEVEL != 256) snprintf(ys, sizeof ys, "Y=%d | ", Y_LEVEL);
        printf("Biome: %s%s | %s1:4 Size: %lld | Blocks^2 Size: %lld\n", biome_name(bm), LB ? " LB" : "", ys, n, n * 16);
        return 0;
    }

    /* LB and other y levels get their own results and checkpoint */
    char tag[32] = "";
    if (LB) strcat(tag, "_LB");
    if (Y_LEVEL != 256) snprintf(tag + strlen(tag), sizeof tag - strlen(tag), "_y%d", Y_LEVEL);
    snprintf(g_results, sizeof g_results, RESULTS_FILE, tag);
    snprintf(g_ckpt, sizeof g_ckpt, CHECKPOINT_FILE, tag);

    long long start = START_SEED;
    int resumed = 0;
    if (argc >= 2) start = strtoll(argv[1], NULL, 10);
    else
    {
        FILE *cf = fopen(g_ckpt, "r");
        if (cf)
        {
            long long v;
            if (fscanf(cf, "%lld", &v) == 1 && v > start) { start = v; resumed = 1; }
            fclose(cf);
        }
    }
    g_start = start;
    atomic_store(&g_next, start);
    atomic_store(&g_head, start);

    g_out = fopen(g_results, "a");
    if (!g_out) { perror(g_results); return 1; }
    fseek(g_out, 0, SEEK_END);
    if (ftell(g_out) == 0) fprintf(g_out, "Seed | Biome | 1:4 Size | Blocks^2 Size\n");
    fprintf(stderr, "%s %lld | %d threads\n", resumed ? "Resume from" : "Start from", start, THREADS);

    signal(SIGINT, on_sigint);
    pthread_t th[THREADS], cth, sth;
    for (int t = 0; t < THREADS; t++) pthread_create(&th[t], NULL, worker, NULL);
    pthread_create(&cth, NULL, ckpt, NULL);
    pthread_create(&sth, NULL, statmon, NULL);
    for (int t = 0; t < THREADS; t++) pthread_join(th[t], NULL);
    g_stop = 1;
    write_checkpoint();
    fprintf(stderr, "Done: %lld seeds | Resume at: %lld\n", atomic_load(&g_head) - start, atomic_load(&g_head));
    fclose(g_out);
    return 0;
}
