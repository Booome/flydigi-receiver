#include "rssi_pick.h"
#include "sle_common.h"
#include "string.h"

#define RSSI_THRESHOLD       50
#define RSSI_FILTER_WIN      8
#define RSSI_HOLD_MS         2000
#define RSSI_SWITCH_DB       3
#define RSSI_SWITCH_HOLD_MS  500
#define RSSI_LOST_MS         1000

#define TICK_MS              10
#define TICKS_HOLD    (RSSI_HOLD_MS / TICK_MS)          /* 200 */
#define TICKS_LOST    (RSSI_LOST_MS / TICK_MS)          /* 100 */
#define TICKS_SWITCH  (RSSI_SWITCH_HOLD_MS / TICK_MS)   /* 50 */

typedef struct {
    uint8_t  addr[SLE_ADDR_LEN];
    int8_t   hist[RSSI_FILTER_WIN];
    uint8_t  n;
    uint32_t last_seen_ticks;
    uint32_t hold_start_ticks;
} cand_t;

static cand_t g_cand = { 0 };
static cand_t g_take = { 0 };
static bool g_locked = false;
static uint32_t g_ticks = 0;

void rssi_pick_tick(void)
{
    g_ticks++;
    /* time-based stale recovery: drop candidates that stopped broadcasting */
    if (g_cand.n != 0 && g_ticks - g_cand.last_seen_ticks > TICKS_LOST) {
        memset(&g_cand, 0, sizeof(g_cand));
    }
    if (g_take.n != 0 && g_ticks - g_take.last_seen_ticks > TICKS_SWITCH) {
        memset(&g_take, 0, sizeof(g_take));
    }
}

void rssi_pick_init(void)
{
    memset(&g_cand, 0, sizeof(g_cand));
    memset(&g_take, 0, sizeof(g_take));
    g_locked = false;
}

const uint8_t *rssi_pick_locked_addr(void) { return g_cand.addr; }

static int8_t cand_rssi_f(const cand_t *c)
{
    if (c->n == 0) return -127;
    int32_t sum = 0;
    for (int i = 0; i < c->n; i++) sum += c->hist[i];
    return (int8_t)(sum / c->n);
}

static void cand_push(cand_t *c, const uint8_t addr[SLE_ADDR_LEN], int8_t rssi)
{
    if (c->n == 0) {
        memcpy(c->addr, addr, SLE_ADDR_LEN);
        c->n = 1;
        c->hist[0] = rssi;
    } else {
        if (c->n < RSSI_FILTER_WIN) c->n++;
        for (int i = RSSI_FILTER_WIN - 1; i > 0; i--) c->hist[i] = c->hist[i - 1];
        c->hist[0] = rssi;
    }
    c->last_seen_ticks = g_ticks;
}

static bool cand_try_lock(cand_t *c)
{
    int8_t rf = cand_rssi_f(c);
    if (rf >= -RSSI_THRESHOLD) {
        if (g_ticks - c->hold_start_ticks >= TICKS_HOLD) {
            g_locked = true;
            return true;
        }
    } else {
        c->hold_start_ticks = g_ticks;  /* dropped below threshold: restart */
    }
    return false;
}

/* feed any broadcast frame; the module decides candidate vs takeover internally */
bool rssi_pick_feed(const uint8_t addr[SLE_ADDR_LEN], int8_t rssi)
{
    if (g_locked) return true;

    if (g_cand.n == 0) {
        cand_push(&g_cand, addr, rssi);
        g_cand.hold_start_ticks = g_ticks;
        return cand_try_lock(&g_cand);
    }

    if (memcmp(g_cand.addr, addr, SLE_ADDR_LEN) == 0) {
        cand_push(&g_cand, addr, rssi);
        return cand_try_lock(&g_cand);
    }

    /* different device: switch candidate only after it stays stronger
     * than the current candidate mean + RSSI_SWITCH_DB for TICKS_SWITCH */
    int8_t cand_rf = cand_rssi_f(&g_cand);
    if (g_take.n == 0 || memcmp(g_take.addr, addr, SLE_ADDR_LEN) != 0) {
        if (rssi <= cand_rf + RSSI_SWITCH_DB) {
            return false;
        }
        memset(&g_take, 0, sizeof(g_take));
        cand_push(&g_take, addr, rssi);
        g_take.hold_start_ticks = g_ticks;
        return false;
    }

    cand_push(&g_take, addr, rssi);
    if (cand_rssi_f(&g_take) <= cand_rf + RSSI_SWITCH_DB) {
        memset(&g_take, 0, sizeof(g_take));
        return false;
    }
    if (g_ticks - g_take.hold_start_ticks >= TICKS_SWITCH) {
        /* takeover committed: promote to candidate, restart hold */
        g_cand = g_take;
        g_cand.hold_start_ticks = g_ticks;
        memset(&g_take, 0, sizeof(g_take));
    }
    return false;
}
