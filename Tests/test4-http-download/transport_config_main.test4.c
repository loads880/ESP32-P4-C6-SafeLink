/**
 * SafeLink Test 4 - deterministic HTTP receive-path stress test.
 *
 * The ESP32-C6 terminates Wi-Fi. The ESP32-P4 downloads a deterministic byte
 * stream over ESP-Hosted SDIO, validates every byte, and immediately discards
 * it. No display, filesystem, SD card, TLS, or large application buffer is
 * involved, so failures are attributable to the network/SDIO receive path.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "sdkconfig.h"

#define MIB                         (1024U * 1024U)
#define WIFI_CONNECTED_BIT          BIT0
#define WIFI_CONNECT_TIMEOUT_MS     30000
#define DOWNLOAD_TIMEOUT_MS         120000
#define BETWEEN_DOWNLOADS_MS        2000

static const char *TAG = "safelink_test4";
static EventGroupHandle_t s_wifi_events;
static unsigned s_disconnect_count;

typedef struct {
    uint64_t expected_bytes;
    uint64_t received_bytes;
    uint64_t next_report;
    uint64_t first_bad_offset;
    uint8_t expected_value;
    uint8_t actual_value;
    bool integrity_failed;
} download_context_t;

static void log_dma(const char *label)
{
    const size_t dma_free =
        heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const size_t dma_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "%s: DMA free=%u largest=%u internal=%u",
             label, (unsigned)dma_free, (unsigned)dma_largest,
             (unsigned)internal_free);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        ++s_disconnect_count;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected (%u), reason=%u; reconnecting",
                 s_disconnect_count,
                 event ? (unsigned)event->reason : 0U);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_connect retry failed: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t init_remote_wifi(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop creation failed: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        ESP_LOGE(TAG, "Could not allocate Wi-Fi event group");
        return ESP_ERR_NO_MEM;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Could not create default Wi-Fi STA interface");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    for (unsigned attempt = 1; ; ++attempt) {
        err = esp_wifi_init(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Remote Wi-Fi initialized on attempt %u", attempt);
            break;
        }
        ESP_LOGW(TAG, "Remote Wi-Fi init attempt %u failed: %s",
                 attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_SAFELINK_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_SAFELINK_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Timed out waiting for a Wi-Fi address");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    download_context_t *ctx = (download_context_t *)event->user_data;

    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    const uint8_t *data = (const uint8_t *)event->data;
    for (int i = 0; i < event->data_len; ++i) {
        const uint8_t expected = (uint8_t)(ctx->received_bytes & 0xffU);
        if (!ctx->integrity_failed && data[i] != expected) {
            ctx->integrity_failed = true;
            ctx->first_bad_offset = ctx->received_bytes;
            ctx->expected_value = expected;
            ctx->actual_value = data[i];
        }
        ++ctx->received_bytes;
    }

    if (ctx->received_bytes >= ctx->next_report) {
        ESP_LOGI(TAG, "Progress: %" PRIu64 " / %" PRIu64 " bytes",
                 ctx->received_bytes, ctx->expected_bytes);
        log_dma("download progress");
        while (ctx->next_report <= ctx->received_bytes) {
            ctx->next_report += MIB;
        }
    }

    return ESP_OK;
}

static esp_err_t run_download(unsigned size_mib, unsigned sequence)
{
    char url[192];
    int written = snprintf(url, sizeof(url), "http://%s:%d/download/%uMiB.bin",
                           CONFIG_SAFELINK_SERVER_HOST,
                           CONFIG_SAFELINK_SERVER_PORT, size_mib);
    if (written < 0 || written >= (int)sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    download_context_t ctx = {
        .expected_bytes = (uint64_t)size_mib * MIB,
        .received_bytes = 0,
        .next_report = MIB,
    };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        .timeout_ms = DOWNLOAD_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
    };

    ESP_LOGI(TAG, "Download %u started: %s", sequence, url);
    log_dma("before download");
    const int64_t started_us = esp_timer_get_time();

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    const int64_t declared_length = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    const uint64_t elapsed_ms =
        (uint64_t)(esp_timer_get_time() - started_us) / 1000U;
    const uint64_t kib_per_second = elapsed_ms
        ? (ctx.received_bytes * 1000U) / elapsed_ms / 1024U
        : 0;

    log_dma("after download");
    ESP_LOGI(TAG,
             "Download %u result: err=%s HTTP=%d declared=%" PRId64
             " received=%" PRIu64 " elapsed=%" PRIu64 "ms rate=%" PRIu64 "KiB/s",
             sequence, esp_err_to_name(err), status, declared_length,
             ctx.received_bytes, elapsed_ms, kib_per_second);

    if (err != ESP_OK) {
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Unexpected HTTP status %d", status);
        return ESP_FAIL;
    }
    if (declared_length != (int64_t)ctx.expected_bytes ||
        ctx.received_bytes != ctx.expected_bytes) {
        ESP_LOGE(TAG, "Length validation failed");
        return ESP_ERR_INVALID_SIZE;
    }
    if (ctx.integrity_failed) {
        ESP_LOGE(TAG,
                 "Integrity failure at offset=%" PRIu64 " expected=0x%02x actual=0x%02x",
                 ctx.first_bad_offset, ctx.expected_value, ctx.actual_value);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "SafeLink download %u PASS (%u MiB)", sequence, size_mib);
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "SafeLink Test 4 - deterministic HTTP download");
    ESP_LOGI(TAG, "SafeLink fixed RX pool, backpressure and TX retry remain active");
    ESP_LOGI(TAG, "Server: %s:%d", CONFIG_SAFELINK_SERVER_HOST,
             CONFIG_SAFELINK_SERVER_PORT);
    log_dma("boot");

    esp_err_t err = init_remote_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi setup failed: %s", esp_err_to_name(err));
        return;
    }

    esp_hosted_app_desc_t desc = {0};
    if (esp_hosted_get_coprocessor_app_desc(&desc) == ESP_OK) {
        ESP_LOGI(TAG, "C6 project=%s version=%s IDF=%s",
                 desc.project_name, desc.version, desc.idf_ver);
    }

    static const unsigned baseline_sizes_mib[] = {1, 5, 25, 50};
    unsigned sequence = 0;
    for (unsigned i = 0;
         i < sizeof(baseline_sizes_mib) / sizeof(baseline_sizes_mib[0]); ++i) {
        ++sequence;
        err = run_download(baseline_sizes_mib[i], sequence);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Baseline stopped after download %u: %s",
                     sequence, esp_err_to_name(err));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(BETWEEN_DOWNLOADS_MS));
    }

    ESP_LOGI(TAG, "Baseline PASS; beginning repeated 50 MiB endurance test");
    while (true) {
        ++sequence;
        err = run_download(50, sequence);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Endurance stopped after download %u: %s",
                     sequence, esp_err_to_name(err));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(BETWEEN_DOWNLOADS_MS));
    }
}
