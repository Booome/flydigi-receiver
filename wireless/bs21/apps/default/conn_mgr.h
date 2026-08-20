#ifndef CONN_MGR_H
#define CONN_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include "errcode.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"

void conn_mgr_init(void);
void conn_mgr_start(void);

void conn_mgr_on_long_press(void);
void conn_mgr_on_short_press(void);
void conn_mgr_tick(uint32_t now_ms);

bool conn_mgr_record_valid(void);

void conn_mgr_seek_result(sle_seek_result_info_t *result);
void conn_mgr_state_changed(uint16_t conn_id, const sle_addr_t *addr,
                            sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                            sle_disc_reason_t disc_reason);
void conn_mgr_pair_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status);
void conn_mgr_param_update(uint16_t conn_id, errcode_t status,
                           const sle_connection_param_update_evt_t *param);
void conn_mgr_auth_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
                            const sle_auth_info_evt_t *evt);
void conn_mgr_seek_enable(errcode_t status);
void conn_mgr_seek_disable(errcode_t status);

#endif /* CONN_MGR_H */
