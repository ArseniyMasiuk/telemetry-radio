#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ELRS_TX_MAX_PARAMS 64
#define ELRS_TX_PARAM_NAME_LEN 48
#define ELRS_TX_PARAM_VALUE_LEN 128

typedef struct
{
    uint8_t index;
    uint8_t type;
    char name[ELRS_TX_PARAM_NAME_LEN];
    char value[ELRS_TX_PARAM_VALUE_LEN];
} elrs_tx_param_entry_t;

typedef struct
{
    char device_name[32];
    uint32_t serial;
    uint32_t hw_version;
    uint32_t sw_version;
    uint8_t param_count;
    uint8_t param_version;
    uint8_t num_loaded;
    bool device_info_valid;
    elrs_tx_param_entry_t params[ELRS_TX_MAX_PARAMS];
} elrs_tx_params_t;

esp_err_t elrs_tx_params_fetch_all(void);
const elrs_tx_params_t *elrs_tx_params_get(void);
void elrs_tx_params_log_all(void);
