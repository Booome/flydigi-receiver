#include "conn_nv.h"
#include "nv.h"
#include "sle_common.h"
#include "soc_osal.h"

#define CONN_NV_KEY 0x3001
#define NV_RETRY_MAX 3
#define RECORD_VALID 0xAA

typedef struct {
  uint8_t valid;
  uint8_t addr[SLE_ADDR_LEN];
} conn_record_t;

static bool g_fatal = false;

void conn_nv_init(void) { uapi_nv_init(); }

bool conn_nv_load(uint8_t addr_out[6]) {
  conn_record_t rec = {0};
  uint16_t len = 0;
  for (int i = 0; i < NV_RETRY_MAX; i++) {
    errcode_t rc =
        uapi_nv_read(CONN_NV_KEY, sizeof(rec), &len, (uint8_t *)&rec);
    if (rc == ERRCODE_SUCC) {
      if (len == sizeof(rec) && rec.valid == RECORD_VALID) {
        for (int j = 0; j < SLE_ADDR_LEN; j++) {
          addr_out[j] = rec.addr[j];
        }
        return true;
      }
      return false; /* no valid record yet, not an error */
    }
    if (rc == ERRCODE_NV_KEY_NOT_FOUND) {
      return false; /* key never written yet, not an error */
    }
  }
  g_fatal = true;
  osal_printk("[nv] read fatal after retries\r\n");
  return false;
}

static bool nv_write_rec(const conn_record_t *rec) {
  for (int i = 0; i < NV_RETRY_MAX; i++) {
    if (uapi_nv_write(CONN_NV_KEY, (const uint8_t *)rec,
                      sizeof(conn_record_t)) == ERRCODE_SUCC) {
      return true;
    }
  }
  g_fatal = true;
  return false;
}

bool conn_nv_save(const uint8_t addr[6]) {
  conn_record_t rec;
  rec.valid = RECORD_VALID;
  for (int j = 0; j < SLE_ADDR_LEN; j++) {
    rec.addr[j] = addr[j];
  }
  if (!nv_write_rec(&rec)) {
    osal_printk("[nv] write fatal after retries\r\n");
    return false;
  }
  return true;
}

bool conn_nv_erase(void) {
  conn_record_t rec = {0};
  if (!nv_write_rec(&rec)) {
    osal_printk("[nv] erase fatal after retries\r\n");
    return false;
  }
  return true;
}

bool conn_nv_is_fatal(void) { return g_fatal; }
