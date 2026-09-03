# M3 Decoy Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Decoy SLE server skeleton with comprehensive PDU-level logging, capable of running on H3863 and capturing dongle's SSAP communication sequence.

**Architecture:** SLE server app that initializes with a single service/property, registers all SSAP callbacks with detailed logging, and responds minimally to all requests. Runs alongside Probe via reset mutual exclusion.

**Tech Stack:** WS63 SDK (H3863), C, SLE SSAP Server API, cmake build system

## Global Constraints

- All code/comments in English, docs in Chinese
- No `(void)arg` / `unused(arg)` — `-Wno-unused-parameter` already enabled
- C code must be `clang-format -i` after editing
- CMake code must use `cmake-format -c .cmake-format.yaml -i` after editing
- Flashing: use `python3 wireless/tools/burn.py board_a <fwpkg>`, never raw `ws63flash`
- Serial capture: use `python3 wireless/tools/capture_uart.py --board-a --duration 60 --odir /tmp --ts`, never bare screen
- WS63 reset controlled via `uart-gpio` command-line tool, not DTR/RTS
- Worktree must be created in `.worktrees/` directory
- Never commit unless explicitly asked

---

## File Structure

```
.worktrees/m3/                          # Worktree root
└── wireless/bearpi-pico-h3863/
    └── apps/
        └── sle_decoy/                  # New app
            ├── CMakeLists.txt          # App build definition
            ├── main.c                  # Entry point
            ├── sle_decoy.c             # Protocol engine + logging
            ├── sle_decoy.h             # Public interface
            └── sle_decoy_adv.c         # Broadcast configuration
```

---

### Task 1: Create Worktree

**Files:**
- Create: `.worktrees/m3/` (via git worktree)

- [ ] **Step 1: Create worktree and branch**

```bash
git worktree add .worktrees/m3 -b m3
cd .worktrees/m3
```

- [ ] **Step 2: Verify worktree**

```bash
git status
git branch
```

Expected: On branch m3, clean working tree

---

### Task 2: Create Decoy App Skeleton

**Files:**
- Create: `wireless/bearpi-pico-h3863/apps/sle_decoy/CMakeLists.txt`
- Create: `wireless/bearpi-pico-h3863/apps/sle_decoy/main.c`
- Create: `wireless/bearpi-pico-h3863/apps/sle_decoy/sle_decoy.h`
- Create: `wireless/bearpi-pico-h3863/apps/sle_decoy/sle_decoy.c`
- Create: `wireless/bearpi-pico-h3863/apps/sle_decoy/sle_decoy_adv.c`

- [ ] **Step 1: Create directory**

```bash
mkdir -p wireless/bearpi-pico-h3863/apps/sle_decoy
```

- [ ] **Step 2: Create CMakeLists.txt**

```cmake
set(COMPONENT_NAME "main")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sle_decoy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sle_decoy_adv.c
)

set(PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR})

set(PRIVATE_HEADER)

set(PRIVATE_DEFINES)

set(PUBLIC_DEFINES)

set(COMPONENT_CCFLAGS)

set(WHOLE_LINK true)

set(APP_TARGETS "ws63-liteos-app")

set(BUILD_AS_OBJ false)

set(MAIN_COMPONENT false)

build_component()
```

- [ ] **Step 3: Create sle_decoy.h**

```c
#ifndef SLE_DECOY_H
#define SLE_DECOY_H

#include "errcode.h"

errcode_t sle_decoy_init(void);

#endif
```

- [ ] **Step 4: Create main.c**

```c
#include "soc_osal.h"
#include "sle_decoy.h"

static void sle_decoy_task(uintptr_t param) {
    sle_decoy_init();
}

app_task(sle_decoy_task);
```

- [ ] **Step 5: Create sle_decoy.c skeleton**

```c
#include "securec.h"
#include "soc_osal.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_server.h"
#include "sle_decoy.h"

#define SLE_DECOY_LOG "[sle decoy]"

errcode_t sle_decoy_init(void) {
    osal_printk("%s init\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
```

- [ ] **Step 6: Create sle_decoy_adv.c skeleton**

```c
#include "soc_osal.h"
#include "sle_errcode.h"

#define SLE_DECOY_LOG "[sle decoy]"

int sle_decoy_adv_init(void) {
    osal_printk("%s adv init\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
```

- [ ] **Step 7: Format CMakeLists.txt**

```bash
cmake-format -c .cmake-format.yaml -i wireless/bearpi-pico-h3863/apps/sle_decoy/CMakeLists.txt
```

- [ ] **Step 8: Build**

```bash
cd wireless/bearpi-pico-h3863
mkdir -p build && cd build
cmake .. -DFBB_PROJECT_COMPONENT_NAME=sle_decoy
make -j
```

Expected: Build succeeds

---

### Task 3: Add SLE Enable + Connection Callbacks

**Files:**
- Modify: `wireless/bearpi-pico-h3863/apps/sle_decoy/sle_decoy.c`

- [ ] **Step 1: Add includes and connection callback**

Replace sle_decoy.c with:

```c
#include "securec.h"
#include "soc_osal.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_server.h"
#include "sle_decoy.h"

#define SLE_DECOY_LOG "[sle decoy]"
#define SLE_MTU_SIZE 520

static uint8_t g_server_id = 0;
static uint16_t g_conn_hdl = 0;

static void sle_enable_cb(errcode_t status) {
    osal_printk("%s sle_enable status:%x\r\n", SLE_DECOY_LOG, status);
}

static void sle_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                          sle_acb_state_t conn_state,
                                          sle_pair_state_t pair_state,
                                          sle_disc_reason_t disc_reason) {
    osal_printk("%s conn_state conn_id:0x%02x state:0x%x pair:0x%x disc:0x%x\r\n",
                SLE_DECOY_LOG, conn_id, conn_state, pair_state, disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_conn_hdl = conn_id;
        osal_printk("%s CONNECTED\r\n", SLE_DECOY_LOG);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_conn_hdl = 0;
        osal_printk("%s DISCONNECTED\r\n", SLE_DECOY_LOG);
    }
}

static void sle_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("%s pair_complete conn_id:0x%02x status:%x\r\n", SLE_DECOY_LOG, conn_id, status);

    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 1;
    ssaps_set_info(g_server_id, &info);
}

static errcode_t sle_decoy_register_conn_cbks(void) {
    sle_connection_callbacks_t cbk = {0};
    cbk.connect_state_changed_cb = sle_connect_state_changed_cb;
    cbk.pair_complete_cb = sle_pair_complete_cb;
    return sle_connection_register_callbacks(&cbk);
}

errcode_t sle_decoy_init(void) {
    errcode_t ret;

    ret = sle_decoy_register_conn_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn_cbks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail!\r\n", SLE_DECOY_LOG);
        return -1;
    }

    osal_printk("%s init ok\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
```

- [ ] **Step 2: Build**

```bash
cd wireless/bearpi-pico-h3863/build
make -j
```

Expected: Build succeeds

- [ ] **Step 3: Flash and capture boot log**

```bash
cd ../..
python3 wireless/tools/burn.py board_a -a sle_decoy
python3 wireless/tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```

Expected: Serial output shows "init ok" without errors

---

### Task 4: Add SSAP Server with Minimal Service + PDU Logging

**Files:**
- Modify: `wireless/bearpi-pico-h3863/apps/sle_decoy/sle_decoy.c`

- [ ] **Step 1: Add SSAP server with logging callbacks**

Replace sle_decoy.c with:

```c
#include "securec.h"
#include "soc_osal.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_server.h"
#include "sle_decoy.h"

#define SLE_DECOY_LOG "[sle decoy]"
#define SLE_MTU_SIZE 520

#define SLE_SERVICE_UUID 0x1234
#define SLE_PROPERTY_UUID 0x5678

static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;
static uint16_t g_conn_hdl = 0;

static uint8_t g_sle_base_uuid[] = {0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
                                     0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void encode2byte_little(uint8_t *ptr, uint16_t data) {
    *(uint8_t *)(ptr + 1) = (uint8_t)(data >> 0x8);
    *(uint8_t *)ptr = (uint8_t)data;
}

static void sle_set_uuid_base(sle_uuid_t *out) {
    if (memcpy_s(out->uuid, SLE_UUID_LEN, g_sle_base_uuid, SLE_UUID_LEN) != EOK) {
        out->len = 0;
        return;
    }
    out->len = 2;
}

static void sle_set_uuid_u2(uint16_t u2, sle_uuid_t *out) {
    sle_set_uuid_base(out);
    out->len = 2;
    encode2byte_little(&out->uuid[14], u2);
}

static void ssaps_mtu_changed_cb(uint8_t server_id, uint16_t conn_id,
                                  ssap_exchange_info_t *mtu_size, errcode_t status) {
    osal_printk("[SSAP] mtu_changed mtu=%d status=%x\r\n", mtu_size->mtu_size, status);
}

static void ssaps_start_service_cb(uint8_t server_id, uint16_t handle, errcode_t status) {
    osal_printk("[SSAP] start_service handle=%x status=%x\r\n", handle, status);
}

static void ssaps_add_service_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle,
                                  errcode_t status) {
    osal_printk("[SSAP] add_service handle=%x status=%x\r\n", handle, status);
}

static void ssaps_add_property_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle,
                                   uint16_t handle, errcode_t status) {
    osal_printk("[SSAP] add_property svc_hdl=%x prop_hdl=%x status=%x\r\n",
                service_handle, handle, status);
}

static void sle_read_request_cb(uint8_t server_id, uint16_t conn_id,
                                 ssaps_req_read_cb_t *read_cb_para, errcode_t status) {
    osal_printk("[SSAP][RCV] read_req handle=0x%04x type=%d need_rsp=%d\r\n",
                read_cb_para->handle, read_cb_para->type, read_cb_para->need_rsp);

    if (read_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = read_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        rsp.value = NULL;
        rsp.value_len = 0;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] read_rsp empty\r\n");
    }
}

static void sle_write_request_cb(uint8_t server_id, uint16_t conn_id,
                                  ssaps_req_write_cb_t *write_cb_para, errcode_t status) {
    osal_printk("[SSAP][RCV] write_req handle=0x%04x len=%d need_rsp=%d data:",
                write_cb_para->handle, write_cb_para->length, write_cb_para->need_rsp);
    for (uint16_t i = 0; i < write_cb_para->length; i++) {
        osal_printk("%02x ", write_cb_para->value[i]);
    }
    osal_printk("\r\n");

    if (write_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] write_rsp ok\r\n");
    }
}

static void sle_read_by_uuid_request_cb(uint8_t server_id, uint16_t conn_id,
                                         ssaps_req_read_by_uuid_cb_t *read_cb_para,
                                         errcode_t status) {
    osal_printk("[SSAP][RCV] read_by_uuid_req uuid=");
    for (uint8_t i = 0; i < read_cb_para->uuid.len; i++) {
        osal_printk("%02x", read_cb_para->uuid.uuid[i]);
    }
    osal_printk(" need_rsp=%d\r\n", read_cb_para->need_rsp);

    if (read_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = read_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        rsp.value = NULL;
        rsp.value_len = 0;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] read_by_uuid_rsp empty\r\n");
    }
}

static errcode_t sle_decoy_register_ssaps_cbks(void) {
    ssaps_callbacks_t cbk = {0};
    cbk.add_service_cb = ssaps_add_service_cb;
    cbk.add_property_cb = ssaps_add_property_cb;
    cbk.start_service_cb = ssaps_start_service_cb;
    cbk.mtu_changed_cb = ssaps_mtu_changed_cb;
    cbk.read_request_cb = sle_read_request_cb;
    cbk.write_request_cb = sle_write_request_cb;
    cbk.read_by_uuid_request_cb = sle_read_by_uuid_request_cb;
    return ssaps_register_callbacks(&cbk);
}

static errcode_t sle_decoy_add_service(void) {
    sle_uuid_t service_uuid = {0};
    sle_set_uuid_u2(SLE_SERVICE_UUID, &service_uuid);
    uint16_t ret = ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_service fail:%x\r\n", ret);
        return ret;
    }
    osal_printk("[SSAP] service added handle=%x\r\n", g_service_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_decoy_add_property(void) {
    ssaps_property_info_t property = {0};
    uint8_t value[] = {0x01, 0x02, 0x03, 0x04};

    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication =
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    sle_set_uuid_u2(SLE_PROPERTY_UUID, &property.uuid);
    property.value = value;
    property.value_len = sizeof(value);

    uint16_t ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property,
                                            &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_property fail:%x\r\n", ret);
        return ret;
    }
    osal_printk("[SSAP] property added handle=%x\r\n", g_property_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_decoy_start(void) {
    sle_uuid_t app_uuid = {0};
    uint8_t app_uuid_data[2] = {0x12, 0x34};
    app_uuid.len = 2;
    if (memcpy_s(app_uuid.uuid, 2, app_uuid_data, 2) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    ssaps_register_server(&app_uuid, &g_server_id);

    errcode_t ret = sle_decoy_add_service();
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = sle_decoy_add_property();
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] start_service fail:%x\r\n", ret);
        return ret;
    }

    osal_printk("[SSAP] server started server_id=%x\r\n", g_server_id);
    return ERRCODE_SLE_SUCCESS;
}

static void sle_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                          sle_acb_state_t conn_state,
                                          sle_pair_state_t pair_state,
                                          sle_disc_reason_t disc_reason) {
    osal_printk("[CONN] state conn_id:0x%02x state:0x%x pair:0x%x disc:0x%x\r\n",
                conn_id, conn_state, pair_state, disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_conn_hdl = conn_id;
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_conn_hdl = 0;
    }
}

static void sle_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("[PAIR] complete conn_id:0x%02x status:%x\r\n", conn_id, status);

    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 1;
    ssaps_set_info(g_server_id, &info);
}

static errcode_t sle_decoy_register_conn_cbks(void) {
    sle_connection_callbacks_t cbk = {0};
    cbk.connect_state_changed_cb = sle_connect_state_changed_cb;
    cbk.pair_complete_cb = sle_pair_complete_cb;
    return sle_connection_register_callbacks(&cbk);
}

errcode_t sle_decoy_init(void) {
    errcode_t ret;

    ret = sle_decoy_register_conn_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn_cbks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    ret = sle_decoy_register_ssaps_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_cbks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    ret = sle_decoy_start();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    ret = sle_decoy_adv_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv_init fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    osal_printk("%s init ok\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
```

- [ ] **Step 2: Build**

```bash
cd wireless/bearpi-pico-h3863/build
make -j
```

Expected: Build succeeds

---

### Task 5: Add Broadcast Configuration

**Files:**
- Modify: `wireless/bearpi-pico-h3863/apps/sle_decoy/sle_decoy_adv.c`

- [ ] **Step 1: Replace sle_decoy_adv.c with broadcast implementation**

Replace with (adapted from sle_server_adv.c):

```c
#include "securec.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_decoy.h"

#define SLE_ADV_TX_POWER_DBM 18

#define SLE_ADV_INTERVAL_MIN_DEFAULT 0xC8
#define SLE_ADV_INTERVAL_MAX_DEFAULT 0xC8
#define SLE_CONN_INTV_MIN_DEFAULT 0x64
#define SLE_CONN_INTV_MAX_DEFAULT 0x64
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT 0x1F4
#define SLE_CONN_MAX_LATENCY 0x1F3

#define SLE_DECOY_NAME "flydigi_m3"

#define SLE_DECOY_LOG "[sle decoy]"

typedef struct sle_adv_common_value {
    uint8_t length;
    uint8_t type;
    uint8_t value;
} le_adv_common_t;

typedef enum sle_adv_channel {
    SLE_ADV_CHANNEL_MAP_77 = 0x01,
    SLE_ADV_CHANNEL_MAP_78 = 0x02,
    SLE_ADV_CHANNEL_MAP_79 = 0x04,
    SLE_ADV_CHANNEL_MAP_DEFAULT = 0x07
} sle_adv_channel_map_t;

typedef enum sle_adv_data {
    SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL = 0x01,
    SLE_ADV_DATA_TYPE_ACCESS_MODE = 0x02,
    SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME = 0x0B,
    SLE_ADV_DATA_TYPE_TX_POWER_LEVEL = 0x0C
} sle_adv_data_type;

static uint8_t g_sle_local_name[] = SLE_DECOY_NAME;

static uint16_t sle_set_adv_local_name(uint8_t *adv_data, uint16_t max_len) {
    errno_t ret;
    uint8_t index = 0;
    uint8_t local_name_len = sizeof(g_sle_local_name) - 1;

    adv_data[index++] = local_name_len + 1;
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    ret = memcpy_s(&adv_data[index], max_len - index, g_sle_local_name, local_name_len);
    if (ret != EOK) {
        osal_printk("%s memcpy fail\r\n", SLE_DECOY_LOG);
        return 0;
    }
    return (uint16_t)index + local_name_len;
}

static uint16_t sle_set_adv_data(uint8_t *adv_data) {
    size_t len = 0;
    uint16_t idx = 0;
    errno_t ret = 0;

    len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_disc_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, len);
    if (ret != EOK) {
        return 0;
    }
    idx += len;

    len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_access_mode = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0,
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_access_mode, len);
    if (ret != EOK) {
        return 0;
    }
    idx += len;

    return idx;
}

static uint16_t sle_set_scan_response_data(uint8_t *scan_rsp_data) {
    uint16_t idx = 0;
    errno_t ret;
    size_t scan_rsp_data_len = sizeof(struct sle_adv_common_value);

    struct sle_adv_common_value tx_power_level = {
        .length = scan_rsp_data_len - 1,
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER_DBM,
    };
    ret = memcpy_s(scan_rsp_data, SLE_ADV_DATA_LEN_MAX, &tx_power_level, scan_rsp_data_len);
    if (ret != EOK) {
        return 0;
    }
    idx += scan_rsp_data_len;

    idx += sle_set_adv_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
    return idx;
}

static int sle_set_default_announce_param(void) {
    errno_t ret;
    sle_announce_param_t param = {0};
    unsigned char local_addr[SLE_ADDR_LEN] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = 1;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_ADV_INTERVAL_MIN_DEFAULT;
    param.announce_interval_max = SLE_ADV_INTERVAL_MAX_DEFAULT;
    param.conn_interval_min = SLE_CONN_INTV_MIN_DEFAULT;
    param.conn_interval_max = SLE_CONN_INTV_MAX_DEFAULT;
    param.conn_max_latency = SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT;
    param.announce_tx_power = SLE_ADV_TX_POWER_DBM;
    param.own_addr.type = 0;
    ret = memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN);
    if (ret != EOK) {
        return 0;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static int sle_set_default_announce_data(void) {
    errcode_t ret;
    uint8_t announce_data_len = 0;
    uint8_t seek_data_len = 0;
    sle_announce_data_t data = {0};
    uint8_t adv_handle = 1;
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = {0};

    announce_data_len = sle_set_adv_data(announce_data);
    data.announce_data = announce_data;
    data.announce_data_len = announce_data_len;

    seek_data_len = sle_set_scan_response_data(seek_rsp_data);
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = seek_data_len;

    ret = sle_set_announce_data(adv_handle, &data);
    if (ret == ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set announce data success.\r\n", SLE_DECOY_LOG);
    } else {
        osal_printk("%s set adv param fail.\r\n", SLE_DECOY_LOG);
    }
    return ERRCODE_SLE_SUCCESS;
}

static void sle_decoy_announce_enable_cb(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce enable id:%02x status:%x\r\n", SLE_DECOY_LOG, announce_id, status);
}

static void sle_decoy_announce_disable_cb(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce disable id:%02x status:%x\r\n", SLE_DECOY_LOG, announce_id, status);
}

static void sle_decoy_announce_terminal_cb(uint32_t announce_id) {
    osal_printk("%s announce terminal id:%02x\r\n", SLE_DECOY_LOG, announce_id);
}

int sle_decoy_adv_init(void) {
    errcode_t ret;
    sle_announce_seek_callbacks_t seek_cbks = {0};
    seek_cbks.announce_enable_cb = sle_decoy_announce_enable_cb;
    seek_cbks.announce_disable_cb = sle_decoy_announce_disable_cb;
    seek_cbks.announce_terminal_cb = sle_decoy_announce_terminal_cb;
    ret = sle_announce_seek_register_callbacks(&seek_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register announce callbacks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    sle_set_default_announce_param();
    sle_set_default_announce_data();
    ret = sle_start_announce(1);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start_announce fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    osal_printk("%s start announce success.\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
```

- [ ] **Step 2: Build and flash**

```bash
cd ../..
python3 wireless/tools/burn.py board_a -a sle_decoy
```

- [ ] **Step 3: Capture boot log**

```bash
python3 wireless/tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```

Expected: Log shows SSAP init sequence ending with "init ok" and "start announce success"

---

### Task 6: Verify with Dongle (Step 1 of Iteration)

This task requires user interaction with hardware.

- [ ] **Step 1: Notify user to insert dongle**

```bash
bash tools/notify.sh
```

Then: "请将 dongle 插入真机位，Decoy 正在运行并等待连接"

- [ ] **Step 2: Capture dongle communication**

```bash
python3 wireless/tools/capture_uart.py --board-a --duration 30 --odir /tmp --ts
```

Expected: Log shows dongle's SSAP requests (Exchange Info, Find Structure, Read, etc.)

- [ ] **Step 3: Analyze logs**

Review `/tmp/` output. Identify:
1. What PDUs does dongle send?
2. What parameters does it use?
3. When does it disconnect?

---

## Post-Skeleton: Iterative Development

After Task 6, the iterative cycle begins:

1. **Analyze** Decoy logs to understand dongle's command sequence
2. **Build Probe** to replay same commands against real controller
3. **Capture** real controller's responses
4. **Update Decoy** to return real responses
5. **Repeat** until dongle is fully satisfied

This part cannot be pre-planned — each iteration depends on observations from the previous one.
