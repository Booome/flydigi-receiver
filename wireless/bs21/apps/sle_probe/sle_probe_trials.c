#include "soc_osal.h"
#include "systick.h"
#include "securec.h"
#include "sle_ssap_client.h"
#include "sle_probe_client.h"
#include "sle_probe_trials.h"

#define TRIALS_LOG  "[trials]"

/* Flydigi V2 handshake: 5a a5 <cmd> <len> <payload> <chk>.
   init sequence + enable from docs/reference/flydigi/vader5.toml. */
static const uint8_t g_v2_init[][5] = {
    {0x5a, 0xa5, 0x01, 0x02, 0x03},
    {0x5a, 0xa5, 0xa1, 0x02, 0xa3},
    {0x5a, 0xa5, 0x02, 0x02, 0x04},
    {0x5a, 0xa5, 0x04, 0x02, 0x06},
};
static const uint8_t g_v2_enable[10] =
    {0x5a, 0xa5, 0x11, 0x07, 0xff, 0x01, 0xff, 0xff, 0xff, 0x15};

static void trials_send(const uint8_t *buf, uint8_t len, uint16_t handle)
{
    ssapc_write_param_t wp = { 0 };
    wp.handle = handle;
    wp.type = SSAP_PROPERTY_TYPE_VALUE;
    wp.data_len = len;
    wp.data = (uint8_t *)buf;
    osal_printk("%s wrote", TRIALS_LOG);
    for (uint8_t i = 0; i < len; i++) {
        osal_printk(" %02x", buf[i]);
    }
    osal_printk(" to 0x%x\r\n", handle);
    ssapc_write_req(0, g_conn_id, &wp);
}

void trials_run(void)
{
    osal_printk("%s start V2 init+enable\r\n", TRIALS_LOG);
    for (uint8_t w = 0; w < g_write_cnt; w++) {
        uint16_t h = g_write_hdls[w];
        for (uint8_t i = 0; i < sizeof(g_v2_init) / sizeof(g_v2_init[0]); i++) {
            trials_send(g_v2_init[i], sizeof(g_v2_init[i]), h);
            uapi_systick_delay_ms(50);
        }
        trials_send(g_v2_enable, sizeof(g_v2_enable), h);
        uapi_systick_delay_ms(50);
    }
    osal_printk("%s done; watching for 5a a5 ef stream\r\n", TRIALS_LOG);
}
