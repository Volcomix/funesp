#include "esp_log.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *tag = "volcomix_ble";

// 11724e4b-670a-c399-fe43-4d4a4a281800
static const ble_uuid128_t service_uuid =
    BLE_UUID128_INIT(
        0x00, 0x18, 0x28, 0x4a, 0x4a, 0x4d,
        0x43, 0xfe,
        0x99, 0xc3,
        0x0a, 0x67,
        0x4b, 0x4e, 0x72, 0x11);

// 11724e4b-670a-c399-fe43-4d4a4a281801
static const ble_uuid128_t char_uuid =
    BLE_UUID128_INIT(
        0x01, 0x18, 0x28, 0x4a, 0x4a, 0x4d,
        0x43, 0xfe,
        0x99, 0xc3,
        0x0a, 0x67,
        0x4b, 0x4e, 0x72, 0x11);

static int char_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(tag, "Characteristic written %d", ctxt->om->om_data[0]);

    uint32_t duty = (((ctxt->om->om_data[0] * 2000) / 180 + 500) * 8192) / 20000;
    ESP_LOGI(tag, "Setting duty to %d", duty);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

    return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = &char_uuid.u,
          .access_cb = char_access,
          .flags = BLE_GATT_CHR_F_WRITE_NO_RSP},
         {0},
     }},
    {0},
};

static int on_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(tag, "Connection established");
        }
        else
        {
            ESP_LOGE(tag, "Connection failed; status=%d", event->connect.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(tag, "Disconnected; reason=%d", event->disconnect.reason);
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ESP_LOGI(tag, "BLE stack synced");

    int rc;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};
    const char *name;

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN |
                       BLE_HS_ADV_F_BREDR_UNSUP;

    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "Error setting advertisement data; rc=%d\n", rc);
        return;
    }

    rsp_fields.uuids128 = (ble_uuid128_t[]){service_uuid};
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "Error setting scan response data; rc=%d\n", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, on_gap_event, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "Error enabling advertisement; rc=%d\n", rc);
        return;
    }
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

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = 13,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();

    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set("Volcomix ESP32");
    assert(rc == 0);

    nimble_port_freertos_init(host_task);
}
