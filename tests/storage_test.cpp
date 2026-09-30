// Compile the real port storage module with simulated NVS and wallet boundaries.
// These tests exercise ordering/recovery, not the Cashu protocol or ESP flash.
#include "papers3.h"
#include "cashu_json.hpp"
#include "cashu_cbor.hpp"
#include "wallet_store.hpp"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>

static std::map<std::string, std::string> durable, staged;
static bool write_failure = false, commit_failure = false, debit_failure = false;
static bool crash_before_debit = false, has_proof = true;
static cashu::Token fixture;

esp_err_t nvs_open(const char *, int, nvs_handle_t *h) { *h = 1; staged = durable; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size) {
    auto found = durable.find(key);
    if (found == durable.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *size = found->second.size(); return ESP_OK; }
    if (*size < found->second.size()) return ESP_FAIL;
    memcpy(out, found->second.data(), found->second.size());
    *size = found->second.size(); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size) {
    if (write_failure) return ESP_FAIL;
    staged[key] = std::string(static_cast<const char *>(data), size); return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t, const char *key) { staged.erase(key); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) { if (commit_failure) return ESP_FAIL; durable = staged; return ESP_OK; }
void nvs_close(nvs_handle_t) {}

cashu::Wallet::Wallet(const std::string &url, secp256k1_context *ctx, int slot)
    : mint_url_(url), ctx_(ctx), nvs_slot_(slot) {}
bool cashu::Wallet::remove_proofs(const std::vector<cashu::Proof> &) {
    assert(durable.count("outbox")); // token must already be committed
    if (crash_before_debit) throw std::runtime_error("simulated power loss");
    if (debit_failure) return false;
    has_proof = false; return true;
}
static cashu::Wallet wallet("https://mint.example", nullptr, 0);
cashu::Wallet *wallet_store_get(int slot) { return slot == 0 ? &wallet : nullptr; }
cashu::Wallet *wallet_store_find(const char *url) { return wallet.mint_url() == url ? &wallet : nullptr; }
void wallet_store_lock() {}
void wallet_store_unlock() {}
std::string cashu::serialize_token_v4(const cashu::Token &) { return "cashuB-storage-test-fixture"; }
bool cashu::deserialize_token(const char *raw, cashu::Token &out) {
    if (std::string(raw) != "cashuB-storage-test-fixture") return false;
    out = fixture; return true;
}
void reset() {
    durable.clear(); staged.clear(); has_proof = true;
    write_failure = commit_failure = debit_failure = crash_before_debit = false;
}
int main() {
    fixture.mint = wallet.mint_url(); fixture.unit = "sat";
    fixture.proofs.push_back({"test", 1, "secret", "signature", {}, {}});
    reset(); assert(papers3_storage_recover());
    write_failure = true; assert(!papers3_save_outbox(fixture)); assert(has_proof);
    reset(); commit_failure = true; assert(!papers3_save_outbox(fixture)); assert(has_proof);
    reset(); crash_before_debit = true;
    try { papers3_save_outbox(fixture); assert(false); } catch (const std::runtime_error &) {}
    assert(has_proof && durable.count("outbox"));
    crash_before_debit = false; assert(papers3_storage_recover()); assert(!has_proof);
    assert(papers3_storage_recover()); assert(!has_proof); // replay is idempotent
    assert(!papers3_save_outbox(fixture)); // cannot overwrite an uncollected token
    reset(); debit_failure = true; assert(!papers3_save_outbox(fixture));
    assert(has_proof && durable.count("outbox")); assert(!papers3_storage_recover());
    debit_failure = false; assert(papers3_storage_recover()); assert(!has_proof);
    assert(papers3_clear_outbox()); assert(!has_proof); // deletion is not a refund
    reset(); durable["outbox"] = "corrupt"; assert(!papers3_storage_recover()); assert(has_proof);
    reset(); durable["outbox"] = std::string(65537, 'x'); assert(!papers3_storage_recover());
    reset();
    PaperInvoice invoice;
    invoice.slot = 0; invoice.amount = 128; invoice.mint = wallet.mint_url();
    invoice.quote.quote = "quote-1"; invoice.quote.request = "lnbc-test";
    assert(papers3_save_invoice(invoice));
    PaperInvoice loaded; assert(papers3_load_invoice(loaded));
    assert(loaded.amount == 128 && loaded.slot == 0 && loaded.quote.quote == "quote-1");
    assert(loaded.quote.unit == "sat" && loaded.quote.method == "bolt11");
    assert(papers3_storage_recover());
    invoice.slot = 2; assert(papers3_save_invoice(invoice)); assert(!papers3_storage_recover());
    assert(papers3_clear_invoice()); assert(papers3_load_invoice(loaded)); assert(loaded.slot == -1);
    durable["invoice"] = R"({"slot":0,"amount":-1,"mint":"x","id":"x","request":"x"})";
    assert(!papers3_load_invoice(loaded));
    durable["invoice"] = R"({"slot":0.5,"amount":1,"mint":"x","id":"x","request":"x"})";
    assert(!papers3_load_invoice(loaded));
    durable["invoice"] = "invalid json"; assert(!papers3_storage_recover());
    std::cout << "PASS: outbox commit/debit order, crash recovery, failed writes, failed debit, replay, overwrite rejection, corrupt records, invoice persistence/validation\n";
}
