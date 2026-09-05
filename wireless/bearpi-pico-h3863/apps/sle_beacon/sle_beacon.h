#ifndef SLE_BEACON_H
#define SLE_BEACON_H

#include <stdint.h>
#include "errcode.h"

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
    SLE_ADV_DATA_TYPE_COMPLETE_LIST_OF_16BIT_SERVICE_UUIDS = 0x05,
    SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME = 0x0B,
    SLE_ADV_DATA_TYPE_TX_POWER_LEVEL = 0x0C,
    SLE_ADV_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA = 0xFF
} sle_adv_data_type;

errcode_t sle_beacon_init(void);

#endif /* SLE_BEACON_H */