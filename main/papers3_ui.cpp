#include "papers3.h"
#include "ui.h"
#include "console.h"
#include "wallet_store.hpp"
#include "cashu_json.hpp"
#include "wifi.h"
#include <M5Unified.h>
#include <lgfx/utility/lgfx_qrcode.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_psram.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace {
enum class Screen { home, amount, invoice, pay, export_confirm, outbox, forget, info };
enum class MessageType { invoice, pay };
struct Message {
    MessageType type;
    int slot;
    cashu::MeltQuote melt;
    PaperInvoice invoice;
    std::string request;
};
QueueHandle_t messages = nullptr;
std::atomic<bool> dirty{true};
std::atomic<bool> pending_payment{false};
Screen screen = Screen::home;
int selected = 0;
PaperInvoice invoice;
cashu::MeltQuote melt;
int melt_slot = -1;
std::string melt_mint, payment_request, digits, notice;
std::string outbox, export_mint;
int64_t export_balance = 0;
int64_t input_after = 0;
bool last_wifi = false;
unsigned frames = 0;

void text(int x, int y, const std::string &s, int size = 2) {
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(x, y);
    M5.Display.print(s.c_str());
}
void wrapped(int x, int y, const std::string &s, size_t columns = 36, int lines = 8) {
    size_t p = 0;
    while (p < s.size() && lines-- > 0) {
        size_t n = std::min(columns, s.size() - p);
        auto nl = s.find('\n', p);
        if (nl != std::string::npos && nl < p + n) n = nl - p;
        text(x, y, s.substr(p, n));
        p += n;
        if (p < s.size() && s[p] == '\n') ++p;
        y += 30;
    }
}
void button(int x, int y, int w, const char *label, int h = 64) {
    M5.Display.drawRoundRect(x, y, w, h, 8, TFT_BLACK);
    text(x + 16, y + 23, label);
}
bool hit(int x, int y, int bx, int by, int w, int h = 64) {
    return x >= bx && x < bx + w && y >= by && y < by + h;
}
void begin_frame(const char *title) {
    M5.Display.setEpdMode((++frames % 8 == 0) ? epd_mode_t::epd_quality : epd_mode_t::epd_fast);
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
    text(24, 22, title, 3);
    text(740, 27, wifi_is_connected() ? "Wi-Fi online" : "Wi-Fi offline");
    M5.Display.drawFastHLine(24, 66, 912, TFT_BLACK);
}
void end_frame() {
    M5.Display.endWrite(); M5.Display.display(); M5.Display.waitDisplay();
    input_after = esp_timer_get_time() + 300000;
}
void info(const std::string &message) { notice = message; screen = Screen::info; dirty = true; }
void busy(const char *label) {
    begin_frame("Cashu / PaperS3"); text(40, 180, label, 3);
    text(40, 240, "Keep the device powered on."); end_frame();
}
cashu::Wallet *selected_wallet() {
    auto *w = wallet_store_get(selected);
    if (w) return w;
    for (int i = 0; i < MAX_MINTS; ++i) {
        if ((w = wallet_store_get(i))) { selected = i; return w; }
    }
    return nullptr;
}
std::string amount_text(int64_t amount) { return std::to_string(amount) + " sats"; }
std::string mint_label(const std::string &url) {
    return url.compare(0, 8, "https://") == 0 ? url.substr(8) : url;
}
bool draw_qr(const std::string &payload) {
    // Explicit bounded buffers, >=3 pixels/module and a four-module quiet zone.
    // Never silently crop a bearer token or build a stack-growing QR loop.
    if (payload.empty() || payload.size() > 1800) return false;
    for (uint8_t version = 1; version <= 25; ++version) {
        QRCode qr;
        std::vector<uint8_t> data(lgfx_qrcode_getBufferSize(version));
        if (lgfx_qrcode_initText(&qr, data.data(), version, 0, payload.c_str()) != 0) continue;
        const int scale = 412 / (qr.size + 8);
        if (scale < 3) return false;
        const int left = 30 + (412 - qr.size * scale) / 2;
        const int top = 80 + (412 - qr.size * scale) / 2;
        for (int y = 0; y < qr.size; ++y)
            for (int x = 0; x < qr.size; ++x)
                if (lgfx_qrcode_getModule(&qr, x, y))
                    M5.Display.fillRect(left + x * scale, top + y * scale, scale, scale, TFT_BLACK);
        return true;
    }
    return false;
}
void home() { screen = Screen::home; notice.clear(); dirty = true; }

void draw() {
    if (!wallet_store_try_lock(0)) { dirty = true; return; }
    if (screen == Screen::invoice) {
        PaperInvoice latest;
        if (!papers3_load_invoice(latest)) { notice = "Cannot read saved invoice."; screen = Screen::info; }
        else if (latest.slot < 0) screen = Screen::home;
        else invoice = latest;
    }
    auto *w = selected_wallet();
    const std::string mint = w ? mint_label(w->mint_url()) : "No mint configured";
    const int64_t balance = w ? w->balance_for_unit("sat") : 0;
    wallet_store_unlock();
    begin_frame("Cashu / PaperS3");
    switch (screen) {
    case Screen::home:
        text(40, 100, amount_text(balance), 6);
        wrapped(42, 190, mint, 70, 2);
        text(42, 274, "Selected mint " + std::to_string(selected));
        button(24, 335, 212, "Receive"); button(256, 335, 212, "Pay Lightning");
        button(488, 335, 212, "Send ecash"); button(720, 335, 212, "Next mint");
        button(24, 435, 212, "Saved token"); button(256, 435, 212, "Refresh");
        text(498, 450, "Experimental firmware");
        if (!w) text(40, 235, "USB: mint add https://your-mint");
        break;
    case Screen::amount:
        text(36, 100, "Receive Lightning", 3);
        text(36, 170, (digits.empty() ? "0" : digits) + " sats", 4);
        wrapped(36, 265, mint, 32, 3);
        button(24, 450, 300, "Cancel");
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 3; ++col) {
            const char *keys[] = {"1","2","3","4","5","6","7","8","9","Delete","0","Invoice"};
            button(440 + col * 165, 92 + row * 102, 150, keys[row*3+col], 84);
        }
        break;
    case Screen::invoice:
        if (!draw_qr(invoice.quote.request)) wrapped(40, 180, "Invoice too large for a readable QR. Use USB: invoice show", 28);
        text(490, 95, amount_text(invoice.amount), 4);
        wrapped(490, 160, mint_label(invoice.mint));
        wrapped(490, 255, "Scan with a Lightning wallet. After paying, tap Check payment.");
        button(490, 365, 440, "Check payment");
        button(490, 450, 210, "Home"); button(720, 450, 210, "Discard");
        break;
    case Screen::pay:
        text(40, 96, "Confirm Lightning payment", 3);
        text(40, 150, amount_text(melt.amount), 4);
        text(40, 215, "Routing reserve: " + amount_text(melt.fee_reserve));
        wrapped(40, 255, mint_label(melt_mint), 70, 2);
        wrapped(40, 320, "Invoice: " + payment_request.substr(0, 120), 70, 2);
        text(40, 400, "Mint input fees may also apply.");
        button(24, 460, 440, "Cancel"); button(490, 460, 440, "Pay now");
        break;
    case Screen::export_confirm:
        text(40, 100, "Send all sats from this mint?", 3);
        text(40, 160, amount_text(export_balance), 5);
        wrapped(40, 240, mint_label(export_mint), 65, 2);
        wrapped(40, 310, "The token is bearer money. Anyone with its QR can redeem it. The recipient may pay mint fees.", 65, 3);
        button(24, 460, 440, "Cancel"); button(490, 460, 440, "Create token");
        break;
    case Screen::outbox:
        if (!draw_qr(outbox)) wrapped(40, 180, "Token too large for a readable QR. Use USB command: outbox", 28);
        text(490, 100, "Saved ecash token", 3);
        wrapped(490, 175, "Share this token only with its recipient. This copy survives reboot. Keep it until redemption.");
        button(490, 365, 440, "Home / hide QR");
        button(490, 450, 440, "Remove saved copy...");
        break;
    case Screen::forget:
        text(40, 100, "Remove saved token?", 3);
        wrapped(40, 180, "Only continue after the recipient has redeemed it or you have backed up the full token. Removal cannot be undone and does not return funds to your balance.", 65, 5);
        button(24, 460, 440, "Keep token"); button(490, 460, 440, "Remove copy");
        break;
    case Screen::info:
        wrapped(40, 100, notice, 68, 10);
        button(24, 460, 440, "Home");
        break;
    }
    end_frame();
}

void open_invoice() {
    wallet_store_guard guard;
    if (!papers3_load_invoice(invoice)) { info("Cannot read saved invoice. Storage preserved."); return; }
    if (invoice.slot >= 0) { screen = Screen::invoice; dirty = true; return; }
    if (!selected_wallet()) { info("Add a mint over USB first:\nmint add https://your-mint"); return; }
    digits.clear(); screen = Screen::amount; dirty = true;
}
void make_invoice() {
    const int amount = digits.empty() ? 0 : std::stoi(digits);
    if (amount <= 0) return;
    busy("Requesting invoice...");
    wallet_store_guard guard;
    auto *w = selected_wallet();
    if (!w || !wifi_is_connected()) { info("Connect Wi-Fi before requesting an invoice."); return; }
    PaperInvoice fresh;
    fresh.slot = selected; fresh.amount = amount; fresh.mint = w->mint_url();
    if (!w->load_keysets() || !w->active_keyset_for_mint("sat") ||
        !w->request_mint_quote(amount, "sat", "bolt11", fresh.quote)) {
        info("Invoice request failed. Check Wi-Fi, mint and USB logs."); return;
    }
    if (!papers3_save_invoice(fresh)) { info("Invoice could not be saved. Do not pay it."); return; }
    invoice = fresh; screen = Screen::invoice; dirty = true;
}
void claim_invoice() {
    busy("Checking payment...");
    wallet_store_guard guard;
    PaperInvoice current;
    if (!papers3_load_invoice(current) || current.slot < 0 || current.quote.quote != invoice.quote.quote) {
        info("Saved invoice changed. Open Receive again."); return;
    }
    auto *w = wallet_store_get(invoice.slot);
    if (!wifi_is_connected() || !w || w->mint_url() != invoice.mint) { info("Mint unavailable. Saved invoice retained."); return; }
    cashu::MintQuote status;
    if (!w->check_mint_quote(invoice.quote.quote, "bolt11", status)) { info("Could not check payment. Retry from Receive."); return; }
    if (status.state == "ISSUED") { info("Mint says this invoice was already issued. Check balance and USB logs before discarding the saved invoice."); return; }
    if (status.amount <= 0) status.amount = invoice.amount;
    if (!status.mintable()) { screen = Screen::invoice; dirty = true; return; }
    if (!w->load_keysets() || !w->mint_tokens(invoice.quote, std::min(invoice.amount, status.mintable()))) {
        info("Claim failed or its result is uncertain. Saved invoice retained. Check USB logs before retrying."); return;
    }
    if (!papers3_clear_invoice()) { info("Funds received, but invoice cleanup failed. Check balance."); return; }
    invoice = {}; info("Payment received.");
}
void export_token() {
    wallet_store_guard guard;
    auto *w = selected_wallet();
    if (!w) { info("No mint configured."); return; }
    if (w->mint_url() != export_mint || w->balance_for_unit("sat") != export_balance) {
        info("Balance changed. Review Send ecash again before exporting."); return;
    }
    cashu::Token token; token.mint = w->mint_url(); token.unit = "sat";
    for (const auto &p : w->proofs()) {
        const auto *unit = w->unit_for_proof(p);
        if (unit && *unit == "sat") token.proofs.push_back(p);
    }
    if (token.proofs.empty()) { info("No sat proofs available to export."); return; }
    if (!papers3_save_outbox(token)) {
        // A journal may already exist while its debit failed. No more spending.
        busy("Export interrupted; restarting safely...");
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
        return;
    }
    papers3_load_outbox(outbox); screen = Screen::outbox; dirty = true;
}
void pay_invoice() {
    busy("Paying Lightning invoice...");
    wallet_store_guard guard;
    auto *w = wallet_store_get(melt_slot);
    int change = 0;
    const bool ok = wifi_is_connected() && w && w->mint_url() == melt_mint &&
                    w->melt_tokens(melt, change);
    pending_payment = false;
    info(ok ? "Payment completed." : "Payment failed or result is uncertain. Check the mint and USB logs before retrying.");
}

void touch(int x, int y) {
    switch (screen) {
    case Screen::home:
        if (hit(x,y,24,335,212)) open_invoice();
        else if (hit(x,y,256,335,212)) info("Paste a BOLT11 invoice over USB:\nmelt <invoice>\n\nReview and confirm the payment on this screen.");
        else if (hit(x,y,488,335,212)) {
            wallet_store_guard guard;
            if (!papers3_load_outbox(outbox)) info("Cannot read outbox.");
            else {
                auto *w = selected_wallet();
                export_mint = w ? w->mint_url() : "";
                export_balance = w ? w->balance_for_unit("sat") : 0;
                screen = outbox.empty() ? Screen::export_confirm : Screen::outbox; dirty = true;
            }
        } else if (hit(x,y,720,335,212)) {
            wallet_store_guard guard;
            for (int n = 1; n <= MAX_MINTS; ++n) if (wallet_store_get((selected+n)%MAX_MINTS)) {
                selected = (selected+n)%MAX_MINTS; break;
            }
            dirty = true;
        } else if (hit(x,y,24,435,212)) {
            wallet_store_guard guard;
            if (!papers3_load_outbox(outbox)) info("Cannot read saved token.");
            else if (outbox.empty()) info("No token in the outbox.");
            else { screen = Screen::outbox; dirty = true; }
        } else if (hit(x,y,256,435,212)) { frames = 7; dirty = true; }
        break;
    case Screen::amount:
        if (hit(x,y,24,450,300)) home();
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 3; ++col) {
            if (!hit(x,y,440+col*165,92+row*102,150,84)) continue;
            int key = row*3+col;
            if (key == 11) make_invoice();
            else if (key == 9) { if (!digits.empty()) digits.pop_back(); dirty = true; }
            else if (digits.size() < 7) { digits += key == 10 ? '0' : char('1'+key); dirty = true; }
        }
        break;
    case Screen::invoice:
        if (hit(x,y,490,365,440)) claim_invoice();
        else if (hit(x,y,490,450,210)) home();
        else if (hit(x,y,720,450,210)) info("To discard an unpaid invoice, use USB:\ninvoice discard\n\nDo not discard an invoice you have paid.");
        break;
    case Screen::pay:
        if (hit(x,y,24,460,440)) { pending_payment = false; home(); }
        else if (hit(x,y,490,460,440)) pay_invoice();
        break;
    case Screen::export_confirm:
        if (hit(x,y,24,460,440)) home();
        else if (hit(x,y,490,460,440)) export_token();
        break;
    case Screen::outbox:
        if (hit(x,y,490,365,440)) home();
        else if (hit(x,y,490,450,440)) { screen = Screen::forget; dirty = true; }
        break;
    case Screen::forget:
        if (hit(x,y,24,460,440)) { screen = Screen::outbox; dirty = true; }
        else if (hit(x,y,490,460,440)) {
            wallet_store_guard guard;
            if (papers3_clear_outbox()) { outbox.clear(); home(); }
            else info("Could not remove saved token.");
        }
        break;
    case Screen::info:
        if (hit(x,y,24,460,440)) home();
        break;
    }
}
void ui_task(void *) {
    bool was_down = false;
    for (;;) {
        Message *raw = nullptr;
        if (xQueueReceive(messages, &raw, 0) == pdTRUE) {
            std::unique_ptr<Message> msg(raw);
            if (msg->type == MessageType::pay) {
                melt = msg->melt; melt_slot = msg->slot;
                { wallet_store_guard guard;
                  auto *w = wallet_store_get(melt_slot); melt_mint = w ? w->mint_url() : ""; }
                payment_request = msg->request; screen = Screen::pay;
            } else { invoice = msg->invoice; screen = Screen::invoice; }
            dirty = true;
        }
        if (last_wifi != wifi_is_connected()) { last_wifi = wifi_is_connected(); dirty = true; }
        if (dirty.exchange(false)) draw();
        M5.update();
        const bool down = M5.Touch.getCount() > 0;
        if (down && !was_down && esp_timer_get_time() >= input_after) { const auto point = M5.Touch.getDetail(); touch(point.x, point.y); }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
void cmd_outbox(const char *) {
    wallet_store_guard guard;
    std::string raw;
    if (!papers3_load_outbox(raw)) console_print("error: outbox read failed\r\n");
    else if (raw.empty()) console_print("outbox empty\r\n");
    else { console_print(raw.c_str()); console_print("\r\n"); }
}
void cmd_wifi(const char *arg) {
    // Format preserves spaces in SSIDs and passwords; only the first | splits.
    if (!arg || !strchr(arg, '|')) { console_print("usage: wifi <SSID>|<password> (empty password for open network)\r\n"); return; }
    const char *sep = strchr(arg, '|');
    const size_t ssid_len = sep-arg, pass_len = strlen(sep+1);
    if (!ssid_len || ssid_len > 32 || pass_len > 63 || (pass_len && pass_len < 8)) {
        console_print("error: invalid SSID/password length\r\n"); return;
    }
    wifi_config_t config = {};
    memcpy(config.sta.ssid, arg, ssid_len); memcpy(config.sta.password, sep+1, pass_len);
    config.sta.threshold.authmode = pass_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) { console_print("error: Wi-Fi settings were not saved\r\n"); return; }
    esp_wifi_connect(); console_print("Wi-Fi settings saved; connecting...\r\n");
}
}

void papers3_display_init() {
    auto config = M5.config();
    config.fallback_board = m5::board_t::board_M5PaperS3;
    config.external_display_value = 0;
    config.internal_imu = false; config.internal_spk = false; config.internal_mic = false;
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setAutoDisplay(false);
    M5.Display.setTextWrap(false);
    begin_frame("Cashu / PaperS3"); text(40, 180, "Starting wallet...", 3); end_frame();
    if (!esp_psram_is_initialized() || M5.Display.width() != 960 || M5.Display.height() != 540)
        papers3_fatal("PaperS3 display/PSRAM error", "Check target and octal PSRAM settings.");
}
void papers3_fatal(const char *title, const char *detail) {
    begin_frame(title); wrapped(40, 150, detail, 65); end_frame();
    // Fail closed: no wallet command task may continue after persistence failure.
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
void papers3_start() {
    messages = xQueueCreate(1, sizeof(Message *));
    if (!messages) papers3_fatal("Memory error", "Wallet UI could not start.");
    console_register_cmd("wifi", cmd_wifi, "wifi <SSID>|<password> -- save network");
    console_register_cmd("outbox", cmd_outbox, "print saved outgoing token");
    if (xTaskCreate(ui_task, "papers3_ui", 24576, nullptr, 3, nullptr) != pdPASS)
        papers3_fatal("Memory error", "Wallet UI task could not start.");
}
bool papers3_stage_melt(int slot, const cashu::MeltQuote &quote, const std::string &request) {
    bool expected = false;
    if (!messages || !pending_payment.compare_exchange_strong(expected, true)) return false;
    auto msg = std::unique_ptr<Message>(new (std::nothrow) Message{});
    if (!msg) { pending_payment = false; return false; }
    msg->type = MessageType::pay; msg->slot = slot; msg->melt = quote; msg->request = request;
    Message *raw = msg.get();
    if (xQueueSend(messages, &raw, 0) != pdTRUE) { pending_payment = false; return false; }
    msg.release(); return true;
}
bool papers3_show_invoice(int slot, int amount, const cashu::MintQuote &quote) {
    if (!messages || pending_payment || quote.unit != "sat" || quote.method != "bolt11") return false;
    auto *w = wallet_store_get(slot);
    if (!w) return false;
    auto msg = std::unique_ptr<Message>(new (std::nothrow) Message{});
    if (!msg) return false;
    msg->type = MessageType::invoice;
    msg->invoice = {slot, amount, w->mint_url(), quote};
    if (!papers3_save_invoice(msg->invoice)) return false;
    Message *raw = msg.get();
    if (xQueueSend(messages, &raw, 0) != pdTRUE) return false;
    msg.release(); return true;
}
void ui_refresh() { dirty = true; }
void ui_show_nfc_status(const char *, const char *) { dirty = true; }
void keypad_ui_task(void *) {}
