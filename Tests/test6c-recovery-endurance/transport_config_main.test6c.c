/**
 * SafeLink Test 6C - repeated interrupted HTTP-to-SD recovery endurance.
 *
 * C6 Wi-Fi and ESP-Hosted use P4 SDMMC slot 1 while a FAT32 card remains
 * mounted on slot 0. A known 5 MiB HTTP object is preflighted, streamed through
 * a bounded buffer into a temporary 8.3 file, durably flushed, reopened and
 * byte-verified, atomically renamed, then removed. No resource failure asserts.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

#define MIB                       (1024U * 1024U)
#define DOWNLOAD_BYTES            (5U * MIB)
#define IO_BUFFER_SIZE            4096U
#define STORAGE_RESERVE_BYTES     MIB
#define WIFI_CONNECTED_BIT        BIT0
#define WIFI_CONNECT_TIMEOUT_MS   30000
#define HTTP_TIMEOUT_MS           120000
#define TEST_CYCLES               25U
#define MEMORY_DRIFT_LIMIT        (16U * 1024U)
#define LARGEST_BLOCK_DRIFT_LIMIT (4U * 1024U)
#define SD_MOUNT_POINT            "/sdcard"
#define TEMP_FILE                 SD_MOUNT_POINT "/SL6C.TMP"
#define FINAL_FILE                SD_MOUNT_POINT "/SL6C.BIN"

static const char *TAG = "safelink_test6c";
static EventGroupHandle_t s_wifi_events;
static unsigned s_disconnect_count;

typedef struct {
    FILE *file;
    uint64_t received;
    uint64_t next_report;
    uint64_t first_bad_offset;
    uint8_t expected_value;
    uint8_t actual_value;
    int write_errno;
    bool integrity_failed;
    bool write_failed;
} download_context_t;

typedef struct {
    sdmmc_card_t *card;
    sd_pwr_ctrl_handle_t power;
    bool mounted;
    bool slot_initialized;
} storage_context_t;

typedef struct {
    uint32_t first_dma;
    uint32_t first_largest;
    uint32_t first_internal;
    uint32_t final_dma;
    uint32_t final_largest;
    uint32_t final_internal;
    uint32_t minimum_dma;
    uint32_t minimum_largest;
    uint32_t minimum_internal;
    bool initialized;
} memory_stats_t;

static void log_memory(const char *label)
{
    ESP_LOGI(TAG, "%s: DMA free=%u largest=%u internal=%u", label,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static esp_err_t shared_host_init_noop(void)
{
    ESP_LOGI(TAG, "Preserving ESP-Hosted global SDMMC host initialization");
    return ESP_OK;
}

static esp_err_t shared_host_deinit_noop(void)
{
    ESP_LOGI(TAG, "Suppressing global SDMMC deinit; slot 1 remains active");
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)data;
        ++s_disconnect_count;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected (%u), reason=%u; reconnecting",
                 s_disconnect_count, event ? (unsigned)event->reason : 0U);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi reconnect request failed: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t init_remote_wifi(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) return ESP_ERR_NO_MEM;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    for (unsigned attempt = 1; attempt <= 30; ++attempt) {
        err = esp_wifi_init(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Remote Wi-Fi initialized on attempt %u", attempt);
            break;
        }
        ESP_LOGW(TAG, "Remote Wi-Fi init attempt %u: %s",
                 attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (err != ESP_OK) return err;

    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) return err;

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CONFIG_SAFELINK_WIFI_SSID,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CONFIG_SAFELINK_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) return err;
    if ((err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    if ((err = esp_wifi_connect()) != ESP_OK) return err;

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void remove_named_file(const char *path)
{
    errno = 0;
    if (unlink(path) == 0) {
        ESP_LOGI(TAG, "Removed artifact: %s", path);
    } else if (errno != ENOENT) {
        ESP_LOGW(TAG, "Could not remove %s: errno=%d (%s)",
                 path, errno, strerror(errno));
    }
}

static esp_err_t mount_storage(storage_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = 4};
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &ctx->power);
    if (err != ESP_OK) return err;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.pwr_ctrl_handle = ctx->power;
    host.init = shared_host_init_noop;
    host.deinit = shared_host_deinit_noop;
    const sdmmc_slot_config_t slot = {
        .cd = SDMMC_SLOT_NO_CD, .wp = SDMMC_SLOT_NO_WP,
        .width = 4, .flags = 0,
    };
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024,
    };

    err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount, &ctx->card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Slot 0 mount failed safely: %s", esp_err_to_name(err));
        sdmmc_host_deinit_slot(SDMMC_HOST_SLOT_0);
        sd_pwr_ctrl_del_on_chip_ldo(ctx->power);
        memset(ctx, 0, sizeof(*ctx));
        return err;
    }
    ctx->mounted = true;
    ctx->slot_initialized = true;
    ESP_LOGI(TAG, "Slot 0 FAT32 mounted while SafeLink slot 1 remains active");
    return ESP_OK;
}

static esp_err_t release_storage(storage_context_t *ctx)
{
    esp_err_t result = ESP_OK;
    if (ctx->mounted) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, ctx->card);
        ESP_LOGI(TAG, "Filesystem unmount: %s", esp_err_to_name(err));
        if (result == ESP_OK && err != ESP_OK) result = err;
        ctx->mounted = false;
    }
    if (ctx->slot_initialized) {
        esp_err_t err = sdmmc_host_deinit_slot(SDMMC_HOST_SLOT_0);
        ESP_LOGI(TAG, "Slot 0 release: %s", esp_err_to_name(err));
        if (result == ESP_OK && err != ESP_OK) result = err;
        ctx->slot_initialized = false;
    }
    if (ctx->power != NULL) {
        esp_err_t err = sd_pwr_ctrl_del_on_chip_ldo(ctx->power);
        ESP_LOGI(TAG, "LDO channel 4 release: %s", esp_err_to_name(err));
        if (result == ESP_OK && err != ESP_OK) result = err;
        ctx->power = NULL;
    }
    return result;
}

static esp_err_t make_url(char *url, size_t size, const char *fault_name)
{
    int count;
    if (fault_name != NULL) {
        count = snprintf(url, size,
                         "http://%s:%d/fault/drop/5MiB-at-%s.bin",
                         CONFIG_SAFELINK_SERVER_HOST,
                         CONFIG_SAFELINK_SERVER_PORT, fault_name);
    } else {
        count = snprintf(url, size, "http://%s:%d/download/5MiB.bin",
                         CONFIG_SAFELINK_SERVER_HOST,
                         CONFIG_SAFELINK_SERVER_PORT);
    }
    return count >= 0 && count < (int)size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t require_artifact_absent(const char *path)
{
    struct stat st;
    errno = 0;
    if (stat(path, &st) == 0) {
        ESP_LOGE(TAG, "Unexpected partial artifact remains: %s (%" PRIu64 " bytes)",
                 path, (uint64_t)st.st_size);
        return ESP_FAIL;
    }
    if (errno != ENOENT) {
        ESP_LOGE(TAG, "Could not verify artifact absence: %s errno=%d (%s)",
                 path, errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Partial artifact absent: %s", path);
    return ESP_OK;
}

static esp_err_t query_content_length(const char *url, int64_t *length)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    *length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HEAD preflight: err=%s HTTP=%d Content-Length=%" PRId64,
             esp_err_to_name(err), status, *length);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) return err;
    if (status != 200) return ESP_FAIL;
    return *length >= 0 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t storage_preflight(int64_t declared)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_bytes);
    if (err != ESP_OK) return err;
    uint64_t usable = free_bytes > STORAGE_RESERVE_BYTES
                          ? free_bytes - STORAGE_RESERVE_BYTES : 0;
    ESP_LOGI(TAG,
             "Storage preflight: total=%" PRIu64 " free=%" PRIu64
             " reserve=%u usable=%" PRIu64 " declared=%" PRId64,
             total, free_bytes, STORAGE_RESERVE_BYTES, usable, declared);
    if (declared < 0 || (uint64_t)declared > usable) {
        ESP_LOGW(TAG, "SafeLink storage preflight: insufficient space; file not opened");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Storage preflight PASS");
    return ESP_OK;
}

static esp_err_t http_data_handler(esp_http_client_event_t *event)
{
    download_context_t *ctx = (download_context_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if (ctx == NULL || ctx->file == NULL) return ESP_ERR_INVALID_STATE;

    const uint8_t *bytes = (const uint8_t *)event->data;
    for (int i = 0; i < event->data_len; ++i) {
        uint8_t expected = (uint8_t)((ctx->received + (uint64_t)i) & 0xffU);
        if (bytes[i] != expected) {
            ctx->integrity_failed = true;
            ctx->first_bad_offset = ctx->received + (uint64_t)i;
            ctx->expected_value = expected;
            ctx->actual_value = bytes[i];
            ESP_LOGE(TAG,
                     "Network integrity failure at offset=%" PRIu64
                     " expected=0x%02x actual=0x%02x",
                     ctx->first_bad_offset, expected, bytes[i]);
            return ESP_ERR_INVALID_CRC;
        }
    }

    errno = 0;
    size_t written = fwrite(event->data, 1, (size_t)event->data_len, ctx->file);
    if (written != (size_t)event->data_len) {
        ctx->write_failed = true;
        ctx->write_errno = errno;
        ctx->received += written;
        ESP_LOGE(TAG,
                 "Short SD write handled: requested=%d written=%u total=%" PRIu64
                 " errno=%d (%s)", event->data_len, (unsigned)written,
                 ctx->received, errno, strerror(errno));
        return ESP_ERR_NO_MEM;
    }
    ctx->received += written;

    if (ctx->received >= ctx->next_report) {
        ESP_LOGI(TAG, "HTTP-to-SD progress: %" PRIu64 " / %u bytes",
                 ctx->received, DOWNLOAD_BYTES);
        log_memory("stream progress");
        while (ctx->next_report <= ctx->received) ctx->next_report += MIB;
    }
    return ESP_OK;
}

static esp_err_t download_to_temp(const char *url, int64_t declared)
{
    remove_named_file(TEMP_FILE);
    remove_named_file(FINAL_FILE);

    FILE *file = fopen(TEMP_FILE, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Temporary file open failed: errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }

    download_context_t ctx = {
        .file = file,
        .next_report = MIB,
    };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_data_handler,
        .user_data = &ctx,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        fclose(file);
        remove_named_file(TEMP_FILE);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "HTTP-to-SD run started: %s -> %s", url, TEMP_FILE);
    log_memory("before freight run");
    const int64_t started = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t response_length = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && fflush(file) != 0) {
        ESP_LOGE(TAG, "fflush failed: errno=%d (%s)", errno, strerror(errno));
        err = ESP_FAIL;
    }
    if (err == ESP_OK && fsync(fileno(file)) != 0) {
        ESP_LOGE(TAG, "fsync failed: errno=%d (%s)", errno, strerror(errno));
        err = ESP_FAIL;
    }
    if (fclose(file) != 0 && err == ESP_OK) {
        ESP_LOGE(TAG, "fclose failed: errno=%d (%s)", errno, strerror(errno));
        err = ESP_FAIL;
    }
    ctx.file = NULL;

    const uint64_t elapsed_ms = (uint64_t)(esp_timer_get_time() - started) / 1000U;
    ESP_LOGI(TAG,
             "Freight result: err=%s HTTP=%d HEAD=%" PRId64
             " GET=%" PRId64 " received=%" PRIu64
             " elapsed=%" PRIu64 "ms rate=%" PRIu64 "KiB/s",
             esp_err_to_name(err), status, declared, response_length,
             ctx.received, elapsed_ms,
             elapsed_ms ? (ctx.received * 1000U / elapsed_ms / 1024U) : 0);
    log_memory("after freight run");

    if (err == ESP_OK && status != 200) err = ESP_FAIL;
    if (err == ESP_OK && (declared != DOWNLOAD_BYTES ||
                          response_length != declared ||
                          ctx.received != (uint64_t)declared)) {
        ESP_LOGE(TAG, "Length validation failed");
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && ctx.integrity_failed) err = ESP_ERR_INVALID_CRC;
    if (err == ESP_OK && ctx.write_failed) err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) remove_named_file(TEMP_FILE);
    return err;
}

static esp_err_t verify_and_commit(void)
{
    uint8_t *buffer = malloc(IO_BUFFER_SIZE);
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    FILE *file = fopen(TEMP_FILE, "rb");
    if (file == NULL) {
        free(buffer);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Beginning second-pass SD verification");
    uint64_t offset = 0;
    esp_err_t result = ESP_OK;
    while (offset < DOWNLOAD_BYTES) {
        size_t wanted = IO_BUFFER_SIZE;
        if (wanted > DOWNLOAD_BYTES - offset) wanted = (size_t)(DOWNLOAD_BYTES - offset);
        size_t got = fread(buffer, 1, wanted, file);
        if (got != wanted) {
            ESP_LOGE(TAG, "Verification short read at offset=%" PRIu64, offset);
            result = ESP_FAIL;
            break;
        }
        for (size_t i = 0; i < got; ++i) {
            uint8_t expected = (uint8_t)((offset + i) & 0xffU);
            if (buffer[i] != expected) {
                ESP_LOGE(TAG,
                         "Stored-file integrity failure at offset=%" PRIu64
                         " expected=0x%02x actual=0x%02x",
                         offset + i, expected, buffer[i]);
                result = ESP_ERR_INVALID_CRC;
                break;
            }
        }
        if (result != ESP_OK) break;
        offset += got;
    }
    if (fclose(file) != 0 && result == ESP_OK) result = ESP_FAIL;
    free(buffer);
    if (result != ESP_OK) {
        remove_named_file(TEMP_FILE);
        return result;
    }
    ESP_LOGI(TAG, "Second-pass SD verification PASS: %u bytes", DOWNLOAD_BYTES);

    errno = 0;
    if (rename(TEMP_FILE, FINAL_FILE) != 0) {
        ESP_LOGE(TAG, "Atomic rename failed: errno=%d (%s)", errno, strerror(errno));
        remove_named_file(TEMP_FILE);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Atomic commit PASS: %s", FINAL_FILE);

    struct stat st;
    if (stat(FINAL_FILE, &st) != 0 || (uint64_t)st.st_size != DOWNLOAD_BYTES) {
        ESP_LOGE(TAG, "Committed-file size validation failed");
        remove_named_file(FINAL_FILE);
        return ESP_ERR_INVALID_SIZE;
    }
    if (unlink(FINAL_FILE) != 0) {
        ESP_LOGE(TAG, "Final cleanup failed: errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Final cleanup PASS; card returned to original contents");
    return ESP_OK;
}

static void record_cycle_memory(memory_stats_t *stats, unsigned cycle)
{
    uint32_t dma = (uint32_t)heap_caps_get_free_size(
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint32_t internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    if (!stats->initialized) {
        stats->first_dma = dma;
        stats->first_largest = largest;
        stats->first_internal = internal;
        stats->minimum_dma = dma;
        stats->minimum_largest = largest;
        stats->minimum_internal = internal;
        stats->initialized = true;
    }
    if (dma < stats->minimum_dma) stats->minimum_dma = dma;
    if (largest < stats->minimum_largest) stats->minimum_largest = largest;
    if (internal < stats->minimum_internal) stats->minimum_internal = internal;
    stats->final_dma = dma;
    stats->final_largest = largest;
    stats->final_internal = internal;

    ESP_LOGI(TAG,
             "Cycle %u settled memory: DMA=%u largest=%u internal=%u",
             cycle, dma, largest, internal);
}

static esp_err_t validate_memory_endurance(const memory_stats_t *stats)
{
    if (!stats->initialized) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG,
             "Memory endurance: first DMA=%u largest=%u internal=%u; "
             "final DMA=%u largest=%u internal=%u; "
             "minimum DMA=%u largest=%u internal=%u",
             stats->first_dma, stats->first_largest, stats->first_internal,
             stats->final_dma, stats->final_largest, stats->final_internal,
             stats->minimum_dma, stats->minimum_largest,
             stats->minimum_internal);

    if (stats->final_dma + MEMORY_DRIFT_LIMIT < stats->first_dma) {
        ESP_LOGE(TAG, "DMA memory drift exceeded %u bytes",
                 MEMORY_DRIFT_LIMIT);
        return ESP_ERR_NO_MEM;
    }
    if (stats->final_internal + MEMORY_DRIFT_LIMIT < stats->first_internal) {
        ESP_LOGE(TAG, "Internal memory drift exceeded %u bytes",
                 MEMORY_DRIFT_LIMIT);
        return ESP_ERR_NO_MEM;
    }
    if (stats->minimum_largest + LARGEST_BLOCK_DRIFT_LIMIT <
        stats->first_largest) {
        ESP_LOGE(TAG, "Largest DMA block drift exceeded %u bytes",
                 LARGEST_BLOCK_DRIFT_LIMIT);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Memory endurance PASS: no material settled-memory drift");
    return ESP_OK;
}

void app_main(void)
{
    static const char *fault_names[] = {"64KiB", "1MiB", "4MiB"};

    ESP_LOGI(TAG, "SafeLink Test 6C - 25-cycle interrupted recovery endurance");
    ESP_LOGI(TAG, "Server: %s:%d", CONFIG_SAFELINK_SERVER_HOST,
             CONFIG_SAFELINK_SERVER_PORT);
    ESP_LOGI(TAG,
             "Fault rotation: 64 KiB, 1 MiB, 4 MiB; no P4 or C6 reboot allowed");
    ESP_LOGI(TAG,
             "No-assert policy: checked I/O, partial-file cleanup and memory drift guard");
    log_memory("test start");

    esp_err_t result = init_remote_wifi();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "TEST 6C FAIL: Wi-Fi setup: %s", esp_err_to_name(result));
        return;
    }
    esp_hosted_app_desc_t desc = {0};
    if (esp_hosted_get_coprocessor_app_desc(&desc) == ESP_OK) {
        ESP_LOGI(TAG, "C6 project=%s version=%s IDF=%s",
                 desc.project_name, desc.version, desc.idf_ver);
    }

    char normal_url[192];
    result = make_url(normal_url, sizeof(normal_url), NULL);
    memory_stats_t memory = {0};
    unsigned completed = 0;

    for (unsigned cycle = 1; result == ESP_OK && cycle <= TEST_CYCLES; ++cycle) {
        const char *fault_name = fault_names[(cycle - 1U) % 3U];
        char fault_url[192];
        storage_context_t storage = {0};
        int64_t declared = -1;

        ESP_LOGI(TAG,
                 "================ CYCLE %u/%u BEGIN: cutoff=%s ================",
                 cycle, TEST_CYCLES, fault_name);

        result = make_url(fault_url, sizeof(fault_url), fault_name);
        if (result == ESP_OK) result = mount_storage(&storage);
        if (result == ESP_OK) result = query_content_length(normal_url, &declared);
        if (result == ESP_OK && declared != DOWNLOAD_BYTES) {
            ESP_LOGE(TAG, "Cycle %u unexpected declared length: %" PRId64,
                     cycle, declared);
            result = ESP_ERR_INVALID_SIZE;
        }
        if (result == ESP_OK) result = storage_preflight(declared);

        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Cycle %u fault phase: disconnect at %s",
                     cycle, fault_name);
            esp_err_t interrupted = download_to_temp(fault_url, declared);
            if (interrupted == ESP_OK) {
                ESP_LOGE(TAG, "Cycle %u interrupted transfer unexpectedly completed",
                         cycle);
                result = ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "Cycle %u expected interruption: %s",
                         cycle, esp_err_to_name(interrupted));
                result = require_artifact_absent(TEMP_FILE);
                if (result == ESP_OK) result = require_artifact_absent(FINAL_FILE);
                if (result == ESP_OK &&
                    !(xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT)) {
                    ESP_LOGE(TAG, "Cycle %u lost Wi-Fi state after interruption",
                             cycle);
                    result = ESP_ERR_INVALID_STATE;
                }
            }
        }

        if (result == ESP_OK) {
            int64_t recovery_declared = -1;
            result = query_content_length(normal_url, &recovery_declared);
            if (result == ESP_OK && recovery_declared != declared) {
                ESP_LOGE(TAG, "Cycle %u recovery HEAD changed: %" PRId64,
                         cycle, recovery_declared);
                result = ESP_ERR_INVALID_SIZE;
            }
        }

        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Cycle %u recovery phase: clean 5 MiB download",
                     cycle);
            result = download_to_temp(normal_url, declared);
        }
        if (result == ESP_OK) result = verify_and_commit();

        if (result != ESP_OK) {
            remove_named_file(TEMP_FILE);
            remove_named_file(FINAL_FILE);
        }
        esp_err_t release_result = release_storage(&storage);
        if (result == ESP_OK && release_result != ESP_OK) result = release_result;

        record_cycle_memory(&memory, cycle);
        if (result == ESP_OK) {
            completed = cycle;
            ESP_LOGI(TAG,
                     "CYCLE %u/%u PASS: cutoff=%s cleaned and recovery verified",
                     cycle, TEST_CYCLES, fault_name);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            ESP_LOGE(TAG, "CYCLE %u/%u FAIL: %s",
                     cycle, TEST_CYCLES, esp_err_to_name(result));
        }
    }

    if (result == ESP_OK && completed != TEST_CYCLES)
        result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = validate_memory_endurance(&memory);

    log_memory("test complete");
    if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "TEST 6C PASS: 25/25 interrupted transfers cleaned up and recovered byte-perfectly without reboot");
    } else {
        ESP_LOGE(TAG, "TEST 6C FAIL after %u/%u cycles: %s",
                 completed, TEST_CYCLES, esp_err_to_name(result));
    }
}
