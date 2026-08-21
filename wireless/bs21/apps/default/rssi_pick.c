#include "rssi_pick.h"
#include "sle_common.h"
#include "systick.h"
#include "string.h"

#define RSSI_THRESHOLD       50
#define RSSI_FILTER_WIN      8
#define RSSI_HOLD_MS         2000
#define RSSI_SWITCH_DB       3
#define RSSI_SWITCH_HOLD_MS  500
#define RSSI_LOST_MS         1000

typedef struct {
    uint8_t  addr[SLE_ADDR_LEN];
    int8_t   hist[RSSI_FILTER_WIN];
    uint8_t  n;
    uint32_t last_seen_ms;
    uint32_t hold_start_ms;
} cand_t;

static cand_t g_cand = { 0 };
static cand_t g_take = { 0 };
static bool g_locked = false;

static uint32_t rssi_now_ms(void)
{
    return (uint32_t)uapi_systick_get_ms();
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
    }
    if (c->n < RSSI_FILTER_WIN) {
        c->n++;
    }
    for (int i = RSSI_FILTER_WIN - 1; i > 0; i--) {
        c->hist[i] = c->hist[i - 1];
    }
    c->hist[0] = rssi;
    c->last_seen_ms = rssi_now_ms();
}

static bool cand_try_lock(cand_t *c)
{
    int8_t rf = cand_rssi_f(c);
    if (rf >= -RSSI_THRESHOLD) {
        if (rssi_now_ms() - c->hold_start_ms >= RSSI_HOLD_MS) {
            g_locked = true;
            return true;
        }
    } else {
        c->hold_start_ms = rssi_now_ms();  /* dropped below threshold: restart */
    }
    return false;
}

/* feed any broadcast frame; the module decides candidate vs takeover internally */
bool rssi_pick_feed(const uint8_t addr[SLE_ADDR_LEN], int8_t rssi)
{
    if (g_locked) return true;

    /* time-based stale recovery: drop candidates that stopped broadcasting */
    if (g_cand.n != 0 && rssi_now_ms() - g_cand.last_seen_ms > RSSI_LOST_MS) {
        memset(&g_cand, 0, sizeof(g_cand));
    }
    if (g_take.n != 0 && rssi_now_ms() - g_take.last_seen_ms > RSSI_SWITCH_HOLD_MS) {
        memset(&g_take, 0, sizeof(g_take));
    }

    if (g_cand.n == 0) {
        cand_push(&g_cand, addr, rssi);
        g_cand.hold_start_ms = rssi_now_ms();
        return cand_try_lock(&g_cand);
    }

    if (memcmp(g_cand.addr, addr, SLE_ADDR_LEN) == 0) {
        cand_push(&g_cand, addr, rssi);
        return cand_try_lock(&g_cand);
    }

    /* different device: switch candidate only after it stays stronger
     * than the current candidate mean + RSSI_SWITCH_DB for RSSI_SWITCH_HOLD_MS */
    int8_t cand_rf = cand_rssi_f(&g_cand);
    if (g_take.n == 0 || memcmp(g_take.addr, addr, SLE_ADDR_LEN) != 0) {
        if (rssi <= cand_rf + RSSI_SWITCH_DB) {
            return false;
        }
        memset(&g_take, 0, sizeof(g_take));
        cand_push(&g_take, addr, rssi);
        g_take.hold_start_ms = rssi_now_ms();
        return false;
    }

    cand_push(&g_take, addr, rssi);
    if (cand_rssi_f(&g_take) <= cand_rf + RSSI_SWITCH_DB) {
        memset(&g_take, 0, sizeof(g_take));
        return false;
    }
    if (rssi_now_ms() - g_take.hold_start_ms >= RSSI_SWITCH_HOLD_MS) {
        /* takeover committed: promote to candidate, restart hold */
        g_cand = g_take;
        g_cand.hold_start_ms = rssi_now_ms();
        memset(&g_take, 0, sizeof(g_take));
    }
    return false;
}