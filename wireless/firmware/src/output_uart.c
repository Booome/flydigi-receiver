#include "output.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <sample_usbd.h>

LOG_MODULE_REGISTER(output_uart, LOG_LEVEL_INF);

static const struct device *const uart_dev =
    DEVICE_DT_GET(DT_NODELABEL(uart0));

int output_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    struct usbd_context *usbd_ctx = sample_usbd_init_device(NULL);
    if (usbd_ctx == NULL) {
        LOG_ERR("Failed to init USB device for Debug CDC");
        return -ENODEV;
    }

    int err = usbd_enable(usbd_ctx);
    if (err) {
        LOG_ERR("Failed to enable USB (err %d)", err);
        return err;
    }

    LOG_INF("UART output initialized (P0.20 TX, 115200 baud)");
    return 0;
}

int output_send(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }
    return 0;
}
