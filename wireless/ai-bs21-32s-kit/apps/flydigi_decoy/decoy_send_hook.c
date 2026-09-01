#include "soc_osal.h"
#include <stdint.h>
#include <string.h>

#ifndef HOOK_LOG
#define HOOK_LOG "[hook]"
#endif

#define DECL_REAL(fn) extern void __real_##fn(void *a0, void *a1, void *a2, void *a3);

/* Find-response marker: opcode 05, fmt 0b, then 2-byte sub-type 0x8x.
   Real controller uses fmt 03 and omits the 2 sub-type bytes, so the
   on-wire bytes are 05 0b 00 8x (decoy) vs 05 03 (real). */
static const uint8_t pat[3] = {0x05, 0x0b, 0x00};

DECL_REAL(cs_pdu_tl_send);
DECL_REAL(cs_task_send);
DECL_REAL(cs_item_value_send);
DECL_REAL(cs_pdu_error_rsp);
DECL_REAL(gle_ssaps_send_response);
DECL_REAL(evt_task_gle_acb_llcp_tx);
DECL_REAL(evt_task_gle_acb_prog_tx);
DECL_REAL(evt_task_gle_acb_send_acb_data);
DECL_REAL(evt_task_gle_acb_prog_llcp);
DECL_REAL(gle_tm_data_recv_core);
DECL_REAL(gle_tm_data_send_core);
DECL_REAL(hci_gle_rx_acb_data);
DECL_REAL(hci_gle_rx_icb_data);
DECL_REAL(btsrv_handle_ssaps_msg);
DECL_REAL(ssaps_read_by_uuid_req_handle);
DECL_REAL(ssaps_event_callback_handler);
DECL_REAL(ssaps_find_handle_by_uuid);
DECL_REAL(ssaps_find_items_by_uuid);
DECL_REAL(ssaps_find_hdl_by_uuid_handle);
DECL_REAL(ssaps_exchange_info_req_handle);
DECL_REAL(ssaps_read_req_handle);
DECL_REAL(ssaps_read_req_cbk_handle);
DECL_REAL(ssaps_start_service_handle);
DECL_REAL(ssaps_send_user_response);
DECL_REAL(ssaps_item_value_rsp);
DECL_REAL(ssaps_item_value_send);
DECL_REAL(ssaps_mtu_req_cbk_handle);
DECL_REAL(btsrv_sle_req_read_cbk);
DECL_REAL(btsrv_sle_req_write_cbk);
DECL_REAL(btsrv_sle_find_hdl_by_uuid_cbk);
DECL_REAL(ssap_recv_data_ind_cbk);
DECL_REAL(ssap_recv_tm_data_cbk);
DECL_REAL(ssap_check_type_by_opcode);
DECL_REAL(sapi_ssaps_find_hdl_by_uuid);
DECL_REAL(gle_ssaps_find_hdl_by_uuid);
DECL_REAL(sapi_ssaps_send_response);

static int in_ram(uint32_t a) {
    return (a >= 0x20000000U && a < 0x20030000U) || (a >= 0x00100000U && a < 0x00180000U);
}

static int scan_buf(const char *tag, const void *p, int max) {
    const uint8_t *b = (const uint8_t *)p;
    int i;
    if (b == NULL || !in_ram((uint32_t)b) || max < 4) {
        return -1;
    }
    for (i = 0; i + 4 <= max; i++) {
        if (b[i] == pat[0] && b[i + 1] == pat[1] && b[i + 2] == pat[2] &&
            (b[i + 3] & 0xF0) == 0x80) {
            return i;
        }
    }
    return -1;
}

void __wrap_cs_pdu_tl_send(void *a0, void *a1, void *a2, void *a3) {
    uint8_t *pb = NULL;
    int p;
    int r = 0;

    if (in_ram((uint32_t)a0)) {
        const uint8_t *h = (const uint8_t *)a0;
        if (h[0] != 0 || h[1] != 0 || h[2] != 0 || h[3] != 0) {
            osal_printk("%s tx a0=%p head: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                        "%02x %02x %02x %02x %02x %02x\r\n",
                        HOOK_LOG, a0, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9],
                        h[10], h[11], h[12], h[13], h[14], h[15]);
        }
        if (h[0] == 0x16) {
            osal_printk("%s 0x16 full: %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                        "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                        "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                        "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                        "%02x %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
                        HOOK_LOG, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10],
                        h[11], h[12], h[13], h[14], h[15], h[16], h[17], h[18], h[19], h[20], h[21],
                        h[22], h[23], h[24], h[25], h[26], h[27], h[28], h[29], h[30], h[31], h[32],
                        h[33], h[34], h[35], h[36], h[37], h[38], h[39], h[40], h[41], h[42], h[43],
                        h[44], h[45], h[46], h[47]);
        }
    }

    p = scan_buf("a0", a0, 4096);
    if (p >= 0) {
        pb = (uint8_t *)a0;
    } else if (in_ram((uint32_t)a1)) {
        p = scan_buf("a1", a1, 4096);
        if (p >= 0) {
            pb = (uint8_t *)a1;
        }
    }

    if (p >= 0 && pb != NULL) {
        int move_len = 64;
        if (pb[p + 3] == 0x82) {
            move_len = 14;
        } else if (pb[p + 3] == 0x87) {
            move_len = 64;
        }
        pb[p + 1] = 0x03;
        memmove(pb + p + 2, pb + p + 4, move_len);
        r = 1;
        osal_printk("%s fix: p=%d sub=0x%02x fmt 0b->03, dropped 00 8x (shift %d)\r\n", HOOK_LOG, p,
                    pb[p + 3], move_len);
    }

    if (r == 0) {
        osal_printk("%s cs_pdu_tl_send: pattern not found\r\n", HOOK_LOG);
    }
    __real_cs_pdu_tl_send(a0, a1, a2, a3);
}

void __wrap_cs_task_send(void *a0, void *a1, void *a2, void *a3) {
    __real_cs_task_send(a0, a1, a2, a3);
}
void __wrap_cs_item_value_send(void *a0, void *a1, void *a2, void *a3) {
    __real_cs_item_value_send(a0, a1, a2, a3);
}
void __wrap_cs_pdu_error_rsp(void *a0, void *a1, void *a2, void *a3) {
    __real_cs_pdu_error_rsp(a0, a1, a2, a3);
}

/* Inbound capture: the transport layer hands up a struct (not a raw PDU).
   Follow all RAM pointer fields to locate the real PDU the dongle sent. */
static int g_tm_dump = 0;
static int g_connected = 0;
static int g_pair_complete = 0;
static int g_tx_dump = 0;

void decoy_mark_connected(void) {
    g_connected = 1;
    g_pair_complete = 0;
}

void decoy_mark_pair_complete(void) {
    g_pair_complete = 1;
    g_tm_dump = 0;
    g_tx_dump = 0;
}

static void dump_n(const char *tag, const void *p, int n) {
    if (p == NULL || !in_ram((uint32_t)p) || n <= 0) {
        return;
    }
    const uint8_t *b = (const uint8_t *)p;
    static const char hx[] = "0123456789abcdef";
    char line[400];
    int o = 0;
    if (n > 128) {
        n = 128;
    }
    for (int i = 0; i < n; i++) {
        line[o++] = hx[(b[i] >> 4) & 0xf];
        line[o++] = hx[b[i] & 0xf];
    }
    line[o] = 0;
    osal_printk("%s %s: %s\r\n", HOOK_LOG, tag, line);
}

void __wrap_gle_ssaps_send_response(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        dump_n("ssaps_rsp a0", a0, 64);
        dump_n("ssaps_rsp a1", a1, 64);
    }
    __real_gle_ssaps_send_response(a0, a1, a2, a3);
}
void __wrap_evt_task_gle_acb_llcp_tx(void *a0, void *a1, void *a2, void *a3) {
    __real_evt_task_gle_acb_llcp_tx(a0, a1, a2, a3);
}
void __wrap_evt_task_gle_acb_prog_tx(void *a0, void *a1, void *a2, void *a3) {
    __real_evt_task_gle_acb_prog_tx(a0, a1, a2, a3);
}
void __wrap_evt_task_gle_acb_send_acb_data(void *a0, void *a1, void *a2, void *a3) {
    __real_evt_task_gle_acb_send_acb_data(a0, a1, a2, a3);
}
void __wrap_evt_task_gle_acb_prog_llcp(void *a0, void *a1, void *a2, void *a3) {
    __real_evt_task_gle_acb_prog_llcp(a0, a1, a2, a3);
}

void __wrap_gle_tm_data_recv_core(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete && g_tm_dump < 80 && in_ram((uint32_t)a0)) {
        const uint8_t *b = (const uint8_t *)a0;
        const uint32_t *w = (const uint32_t *)a0;
        static const uint8_t z[32] = {0};
        const uint8_t *p0 = in_ram(w[0]) ? (const uint8_t *)w[0] : z;
        const uint8_t *p2 = in_ram(w[2]) ? (const uint8_t *)w[2] : z;
        const uint8_t *p4 = in_ram(w[4]) ? (const uint8_t *)w[4] : z;
        const uint8_t *p6 = in_ram(w[6]) ? (const uint8_t *)w[6] : z;
        static const char hx[] = "0123456789abcdef";
        char line[300];
        int o = 0;
        int i;
        for (i = 0; i < 28; i++) {
            line[o++] = hx[(b[i] >> 4) & 0xf];
            line[o++] = hx[b[i] & 0xf];
        }
        line[o] = 0;
        osal_printk("%s RX %s w3=%08x\r\n", HOOK_LOG, line, w[3]);
        dump_n("p0", p0, 64);
        dump_n("p2", p2, 64);
        dump_n("p4", p4, 64);
        dump_n("p6", p6, 64);
        if (in_ram((uint32_t)p0)) {
            const uint32_t *pw = (const uint32_t *)p0;
            for (int j = 0; j < 4; j++) {
                if (in_ram(pw[j])) {
                    const char *tags[] = {"p0[0]->", "p0[1]->", "p0[2]->", "p0[3]->"};
                    dump_n(tags[j], (const void *)pw[j], 64);
                }
            }
        }
        g_tm_dump++;
    }
    __real_gle_tm_data_recv_core(a0, a1, a2, a3);
}

void __wrap_gle_tm_data_send_core(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete && g_tx_dump < 80 && in_ram((uint32_t)a0)) {
        const uint8_t *b = (const uint8_t *)a0;
        const uint32_t *w = (const uint32_t *)a0;
        static const uint8_t z[32] = {0};
        const uint8_t *p0 = in_ram(w[0]) ? (const uint8_t *)w[0] : z;
        const uint8_t *p2 = in_ram(w[2]) ? (const uint8_t *)w[2] : z;
        const uint8_t *p4 = in_ram(w[4]) ? (const uint8_t *)w[4] : z;
        const uint8_t *p6 = in_ram(w[6]) ? (const uint8_t *)w[6] : z;
        static const char hx[] = "0123456789abcdef";
        char line[300];
        int o = 0;
        int i;
        for (i = 0; i < 28; i++) {
            line[o++] = hx[(b[i] >> 4) & 0xf];
            line[o++] = hx[b[i] & 0xf];
        }
        line[o] = 0;
        osal_printk("%s TX %s w3=%08x\r\n", HOOK_LOG, line, w[3]);
        dump_n("p0", p0, 64);
        dump_n("p2", p2, 64);
        dump_n("p4", p4, 64);
        dump_n("p6", p6, 64);
        g_tx_dump++;
    }
    __real_gle_tm_data_send_core(a0, a1, a2, a3);
}

void __wrap_hci_gle_rx_acb_data(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s DLI_ACB a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_hci_gle_rx_acb_data(a0, a1, a2, a3);
}

void __wrap_hci_gle_rx_icb_data(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s DLI_ICB a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_hci_gle_rx_icb_data(a0, a1, a2, a3);
}

static int g_ssap_dump = 0;

static void dump_4(const char *tag, void *p0, void *p1, void *p2, void *p3) {
    static const uint8_t z[32] = {0};
    const uint8_t *b[4];
    int i, j;
    b[0] = (p0 && in_ram((uint32_t)p0)) ? (const uint8_t *)p0 : z;
    b[1] = (p1 && in_ram((uint32_t)p1)) ? (const uint8_t *)p1 : z;
    b[2] = (p2 && in_ram((uint32_t)p2)) ? (const uint8_t *)p2 : z;
    b[3] = (p3 && in_ram((uint32_t)p3)) ? (const uint8_t *)p3 : z;
    static const char hx[] = "0123456789abcdef";
    char line[300];
    int o = 0;
    for (j = 0; j < 4; j++) {
        for (i = 0; i < 32; i++) {
            line[o++] = hx[(b[j][i] >> 4) & 0xf];
            line[o++] = hx[b[j][i] & 0xf];
        }
        line[o++] = ' ';
    }
    line[o] = 0;
    osal_printk("%s %s %s\r\n", HOOK_LOG, tag, line);
}

void __wrap_ssaps_event_callback_handler(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s evt_cb a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_event_callback_handler(a0, a1, a2, a3);
}

void __wrap_ssaps_find_handle_by_uuid(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s REQ find_h_uuid a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_find_handle_by_uuid(a0, a1, a2, a3);
}

void __wrap_ssaps_find_items_by_uuid(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s REQ find_items a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_find_items_by_uuid(a0, a1, a2, a3);
}

void __wrap_btsrv_handle_ssaps_msg(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s ssap_msg a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_btsrv_handle_ssaps_msg(a0, a1, a2, a3);
}

void __wrap_ssaps_read_by_uuid_req_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s REQ read_by_uuid a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_read_by_uuid_req_handle(a0, a1, a2, a3);
}

void __wrap_ssaps_find_hdl_by_uuid_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s REQ find_hdl a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_find_hdl_by_uuid_handle(a0, a1, a2, a3);
}

void __wrap_ssaps_exchange_info_req_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s REQ exch_info a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_exchange_info_req_handle(a0, a1, a2, a3);
}

void __wrap_ssaps_read_req_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_pair_complete) {
        osal_printk("%s REQ read a0=%p a1=%p a2=%p a3=%p\r\n", HOOK_LOG, a0, a1, a2, a3);
        dump_n("  a0", a0, 64);
        dump_n("  a1", a1, 64);
    }
    __real_ssaps_read_req_handle(a0, a1, a2, a3);
}

void __wrap_ssaps_read_req_cbk_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("read_req_cbk", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssaps_read_req_cbk_handle(a0, a1, a2, a3);
}

void __wrap_ssaps_start_service_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("start_svc", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssaps_start_service_handle(a0, a1, a2, a3);
}

void __wrap_ssaps_send_user_response(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("send_usr_rsp", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssaps_send_user_response(a0, a1, a2, a3);
}

void __wrap_ssaps_item_value_rsp(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("item_val_rsp", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssaps_item_value_rsp(a0, a1, a2, a3);
}

void __wrap_ssaps_item_value_send(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("item_val_send", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssaps_item_value_send(a0, a1, a2, a3);
}

void __wrap_ssaps_mtu_req_cbk_handle(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("mtu_req_cbk", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssaps_mtu_req_cbk_handle(a0, a1, a2, a3);
}

void __wrap_btsrv_sle_req_read_cbk(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("btsrv_read_cbk", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_btsrv_sle_req_read_cbk(a0, a1, a2, a3);
}

void __wrap_btsrv_sle_req_write_cbk(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("btsrv_write_cbk", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_btsrv_sle_req_write_cbk(a0, a1, a2, a3);
}

void __wrap_btsrv_sle_find_hdl_by_uuid_cbk(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("btsrv_find_hdl", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_btsrv_sle_find_hdl_by_uuid_cbk(a0, a1, a2, a3);
}

void __wrap_ssap_recv_data_ind_cbk(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("ssap_recv_ind", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssap_recv_data_ind_cbk(a0, a1, a2, a3);
}

void __wrap_ssap_recv_tm_data_cbk(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("ssap_recv_tm", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssap_recv_tm_data_cbk(a0, a1, a2, a3);
}

void __wrap_ssap_check_type_by_opcode(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("ssap_chk_op", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_ssap_check_type_by_opcode(a0, a1, a2, a3);
}

void __wrap_sapi_ssaps_find_hdl_by_uuid(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("sapi_find_hdl", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_sapi_ssaps_find_hdl_by_uuid(a0, a1, a2, a3);
}

void __wrap_gle_ssaps_find_hdl_by_uuid(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("gle_find_hdl", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_gle_ssaps_find_hdl_by_uuid(a0, a1, a2, a3);
}

void __wrap_sapi_ssaps_send_response(void *a0, void *a1, void *a2, void *a3) {
    if (g_ssap_dump < 60) {
        dump_4("sapi_send_rsp", a0, a1, a2, a3);
        g_ssap_dump++;
    }
    __real_sapi_ssaps_send_response(a0, a1, a2, a3);
}
