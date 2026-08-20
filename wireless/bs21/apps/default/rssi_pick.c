#include "rssi_pick.h"
#include "sle_common.h"
#include "string.h"

#define RSSI_THRESHOLD       50
#define RSSI_FILTER_WIN      8
#define RSSI_HOLD_MS         2000
#define RSSI_SWITCH_DB       3
#define RSSI_LOST_MS         1000

#define TICK_MS              10
#define TICKS_HOLD    (RSSI_HOLD_MS / TICK_MS)          /* 200 */
#define TICKS_LOST    (RSSI_LOST_MS / TICK_MS)          /* 100 */

typedef struct {
    uint8_t  addr[SLE_ADDR_LEN];
    int8_t   hist[RSSI_FILTER_WIN];
    uint8_t  n;
    uint32_t last_seen_ticks;
    uint32_t hold_start_ticks;
} cand_t;

static cand_t g_cand = { 0 };
static bool g_locked = false;
static uint32_t g_ticks = 0;

void rssi_pick_tick(void) { g_ticks++; }

void rssi_pick_init(void)
{
    memset(&g_cand, 0, sizeof(g_cand));
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

/* caller decides takeover via is_stronger(); feed only the current best device */
bool rssi_pick_feed(const uint8_t addr[SLE_ADDR_LEN], int8_t rssi)
{
    if (g_locked) return true;

    /* stale candidate: reset */
    if (g_cand.n != 0 && g_ticks - g_cand.last_seen_ticks > TICKS_LOST) {
        memset(&g_cand, 0, sizeof(g_cand));
    }

    if (g_cand.n == 0) {
        memcpy(g_cand.addr, addr, SLE_ADDR_LEN);
        g_cand.n = 1;
        g_cand.hist[0] = rssi;
        g_cand.last_seen_ticks = g_ticks;
        g_cand.hold_start_ticks = g_ticks;
    } else {
        /* same device: slide history window */
        if (g_cand.n < RSSI_FILTER_WIN) g_cand.n++;
        for (int i = RSSI_FILTER_WIN - 1; i > 0; i--) g_cand.hist[i] = g_cand.hist[i - 1];
        g_cand.hist[0] = rssi;
        g_cand.last_seen_ticks = g_ticks;
    }

    int8_t rf = cand_rssi_f(&g_cand);
    if (rf >= -RSSI_THRESHOLD) {
        if (g_ticks - g_cand.hold_start_ticks >= TICKS_HOLD) {
            g_locked = true;
            return true;
        }
    } else {
        g_cand.hold_start_ticks = g_ticks;  /* dropped below threshold: restart */
    }
    return false;
}

/* true if a different device is stronger than the current candidate by hysteresis */
bool rssi_pick_is_stronger(const uint8_t addr[SLE_ADDR_LEN], int8_t rssi)
{
    if (g_cand.n == 0) return true;
    if (memcmp(g_cand.addr, addr, SLE_ADDR_LEN) == 0) return false;
    return rssi > (cand_rssi_f(&g_cand) + RSSI_SWITCH_DB);
}