#include <esp_log.h>
#include "task_config.h"
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "secp256k1.h"
#include "crypto_test.h"
#include "wifi.h"
#include "http.h"
#include "cashu_json.hpp"
#include "wallet.hpp"
#include "keyset.hpp"
#include "unit.hpp"
#include "commands.h"
#include "console.h"
#include "selftest.hpp"
#include "display.h"
#include "i2c_bus.h"
#include "nfc.hpp"
#include "keypad.h"
#include "wallet_store.hpp"
#include "ui.h"
#include "papers3.h"

#define TAG "nucula"

// Boot sequence only — the console command handlers live in the
// commands_*.cpp files.

// Drain task: while WiFi is connected, walk each wallet's pending queue
// and try to swap the stashed offline-receive tokens. We can't drain just
// once on the rising edge: DNS/routing is often not usable for the first
// few seconds after GOT_IP, and a link that stays up never produces
// another edge. So once connected we retry with an exponential backoff
// until everything is redeemed (or the link drops), then re-arm on the
// next reconnect.
static void wifi_drain_task(void *)
{
    EventGroupHandle_t eg = wifi_get_event_group();
    for (;;) {
        xEventGroupWaitBits(eg, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);
        /* Settle: give the IP stack a moment before the first HTTP. */
        vTaskDelay(pdMS_TO_TICKS(2000));

        TickType_t backoff = pdMS_TO_TICKS(5000);
        const TickType_t backoff_max = pdMS_TO_TICKS(60000);
        while (xEventGroupGetBits(eg) & WIFI_CONNECTED_BIT) {
            if (wallet_store_total_pending() == 0)
                break;

            int total_ok = 0, total_fail = 0;
            for (int i = 0; i < MAX_MINTS; i++) {
                // Per-slot guard: released between slots so console
                // commands can interleave with a long drain pass.
                wallet_store_guard guard;
                auto *w = wallet_store_get(i);
                if (!w || w->pending_count() == 0) continue;
                int ok = 0, fail = 0;
                w->drain_pending_tokens(ok, fail);
                total_ok += ok;
                total_fail += fail;
            }
            if (total_ok || total_fail) {
                ESP_LOGI(TAG, "drain: %d ok, %d failed across all slots",
                         total_ok, total_fail);
                ui_refresh();
            }

            if (total_ok > 0) {
                /* Made progress; retry promptly for the rest. */
                backoff = pdMS_TO_TICKS(5000);
                vTaskDelay(backoff);
            } else {
                /* No progress (DNS not ready / mint down). Back off so we
                 * don't hammer the network, capped at backoff_max. */
                vTaskDelay(backoff);
                if (backoff < backoff_max)
                    backoff = backoff * 2 < backoff_max ? backoff * 2
                                                        : backoff_max;
            }
        }

        /* Drained, or the link dropped. Wait until the bit clears so the
         * next reconnect re-arms us. */
        while (xEventGroupGetBits(eg) & WIFI_CONNECTED_BIT)
            vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "nucula cashu wallet");

    // NVS backs the wallet itself (proofs, seed, keysets) — bring it up
    // first and independently of WiFi.
    papers3_display_init();
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        papers3_fatal("Wallet storage error", "Storage preserved. Do not erase flash.");
        return;
    }

    http_init();

    if (wifi_init() != ESP_OK)
        ESP_LOGE(TAG, "wifi failed, continuing offline");

    // Reserve the console's allocations FIRST, while heap is plentiful, so
    // its USB driver + line buffer always succeed. (Initializing it last
    // starved it once WiFi + every wallet's keysets were loaded.) The
    // command TASK starts only after wallet_store_init below, so no
    // handler can ever run against a half-initialized store.
    if (console_init(NULL) != 0) papers3_fatal("USB console failed", "Reboot to retry.");
    commands_wallet_register();
    commands_seed_register();
    commands_system_register();

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) {
        ESP_LOGE(TAG, "failed to create secp256k1 context");
        return;
    }
    {
        unsigned char rand32[32];
        esp_fill_random(rand32, sizeof(rand32));
        if (!secp256k1_context_randomize(ctx, rand32))
            ESP_LOGW(TAG, "secp256k1 context randomize failed");
    }

    if (!wallet_store_init(ctx)) {
        ESP_LOGE(TAG, "wallet store init failed");
        return;
    }


#if CONFIG_NUCULA_SELFTEST_ON_BOOT
    bool tests_ok = crypto_run_tests(ctx) != 0;
    tests_ok = cashu::keyset_run_tests() && tests_ok;
    tests_ok = cashu::unit_run_tests() && tests_ok;
    tests_ok = cashu::cashu_json_run_tests() && tests_ok;
    tests_ok = nucula_pure_selftests() && tests_ok;
    tests_ok = cashu::Wallet::run_tests() && tests_ok;
    if (!tests_ok) papers3_fatal("Wallet self-test failed", "Do not use this build. Check USB logs.");
#endif

    cashu::Wallet::load_seed();
    cashu::Wallet::ensure_p2pk_keypair(wallet_store_ctx());
    // Warm the cache before the keypad/UI tasks exist so later reads
    // from other tasks never hit the lazy NVS load.
    cashu::Wallet::default_unit();

    if (wifi_is_connected()) {
        for (int i = 0; i < MAX_MINTS; i++) {
            auto *w = wallet_store_get(i);
            if (!w) continue;
            if (!w->load_keysets())
                ESP_LOGW(TAG, "failed to refresh keysets for [%d]", i);
        }
    }


    if (!papers3_storage_recover()) {
        papers3_fatal("Outbox recovery failed", "Storage preserved. Reboot to retry.");
        return;
    }
    if (wifi_get_event_group()) xTaskCreate(wifi_drain_task, "wifi_drain", NUCULA_TASK_STACK_WIFI_DRAIN,
                NULL, NUCULA_TASK_PRIO_WIFI_DRAIN, NULL);

    papers3_start();
    if (console_start() != 0) papers3_fatal("USB task failed", "Reboot to retry.");
    ui_refresh();
}
