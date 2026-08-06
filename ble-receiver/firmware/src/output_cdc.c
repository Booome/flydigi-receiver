#include "output.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <sample_usbd.h>

LOG_MODULE_REGISTER(output_cdc, LOG_LEVEL_INF);

static const struct device *const cdc_dev =
    DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static struct usbd_context *usbd_ctx;
static bool cdc_ready;
static K_SEM_DEFINE(dtr_sem, 0, 1);

static void on_usbd_msg(struct usbd_context *const ctx,
                        const struct usbd_msg *msg)
{
    if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
        uint32_t dtr = 0;
        uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
        if (dtr) {
            cdc_ready = true;
            k_sem_give(&dtr_sem);
            LOG_INF("DTR set, CDC ready");
        } else {
            cdc_ready = false;
            LOG_INF("DTR cleared, CDC not ready");
        }
    }
}

int output_init(void)
{
    if (!device_is_ready(cdc_dev)) {
        LOG_ERR("CDC ACM device not ready");
        return -ENODEV;
    }

    usbd_ctx = sample_usbd_init_device(on_usbd_msg);
    if (usbd_ctx == NULL) {
        LOG_ERR("Failed to init USB device");
        return -ENODEV;
    }

    int err = usbd_enable(usbd_ctx);
    if (err) {
        LOG_ERR("Failed to enable USB (err %d)", err);
        return err;
    }

    LOG_INF("USB CDC initialized, waiting for DTR");
    return 0;
}

int output_send(const uint8_t *data, size_t len)
{
    if (!cdc_ready) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(cdc_dev, data[i]);
    }
    return 0;
}
