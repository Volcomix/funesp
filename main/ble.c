#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *tag = "volcomix_ble";

static void on_sync(void)
{
    ESP_LOGI(tag, "BLE stack synced");
}

void host_task(void *param)
{
    ESP_LOGI(tag, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    int rc;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();

    rc = ble_svc_gap_device_name_set("volcomix-esp32");
    assert(rc == 0);

    nimble_port_freertos_init(host_task);
}
