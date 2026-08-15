#include "sle_manager.h"
#include <stdio.h>
#include <string.h>

static scan_result_cb_t g_scan_cb = NULL;
static connect_state_cb_t g_connect_cb = NULL;
static pair_complete_cb_t g_pair_cb = NULL;
static ssap_data_cb_t g_data_cb = NULL;

void sle_init(void)
{
    printf("[SLE] init\n");
}

void sle_start_announce(void)
{
    printf("[SLE] announce started\n");
}

void sle_stop_announce(void)
{
    printf("[SLE] announce stopped\n");
}

void sle_start_seek(void)
{
    printf("[SLE] seek started\n");
}

void sle_stop_seek(void)
{
    printf("[SLE] seek stopped\n");
}

void sle_connect(const uint8_t addr[6])
{
    printf("[SLE] connect to %02X:%02X:%02X:%02X:%02X:%02X\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

void sle_disconnect(uint8_t conn_id)
{
    printf("[SLE] disconnect conn_id=%u\n", conn_id);
}

void sle_pair(uint8_t conn_id)
{
    printf("[SLE] pair conn_id=%u\n", conn_id);
}

void sle_send(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    printf("[SLE] send conn_id=%u len=%u\n", conn_id, len);
}

void sle_set_scan_callback(scan_result_cb_t cb) { g_scan_cb = cb; }
void sle_set_connect_callback(connect_state_cb_t cb) { g_connect_cb = cb; }
void sle_set_pair_callback(pair_complete_cb_t cb) { g_pair_cb = cb; }
void sle_set_data_callback(ssap_data_cb_t cb) { g_data_cb = cb; }