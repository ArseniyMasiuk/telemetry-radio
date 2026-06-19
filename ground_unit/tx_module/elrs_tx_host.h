#pragma once

#include "esp_err.h"
#include "crsf_protocol.h"

esp_err_t elrs_tx_host_init(void);
esp_err_t elrs_tx_host_start(void);
esp_err_t elrs_tx_host_send_frame(const uint8_t *data, size_t len);

// Arm the response waiter for the given frame type. Must be called BEFORE
// sending the request so that a fast response is never lost.
void elrs_tx_host_arm_wait(uint8_t type);

// Collect the frame previously armed with elrs_tx_host_arm_wait. Blocks
// for up to timeout_ms. Disarms on timeout.
esp_err_t elrs_tx_host_collect_frame(uint32_t timeout_ms, crsf_frame_t *out);
