#ifndef SLE_SERVER_H
#define SLE_SERVER_H

#include "errcode.h"
#include "sle_errcode.h"

#define SLE_ADV_HANDLE_DEFAULT 1
#define SLE_ADV_DATA_LEN_MAX 251

errcode_t sle_server_init(void);
errcode_t sle_server_announce_register_cbks(void);
errcode_t sle_server_adv_init(void);

#endif
