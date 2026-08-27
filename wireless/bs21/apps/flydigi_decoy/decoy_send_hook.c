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
void __wrap_gle_ssaps_send_response(void *a0, void *a1, void *a2, void *a3) {
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
