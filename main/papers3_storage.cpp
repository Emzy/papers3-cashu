#include "papers3.h"
#include "cashu_cbor.hpp"
#include "cashu_json.hpp"
#include "wallet_store.hpp"
#include <cJSON.h>
#include <nvs.h>
#include <climits>
#include <cmath>

namespace {
bool read_record(const char *key, std::string &out) {
    out.clear();
    nvs_handle_t h;
    esp_err_t e = nvs_open("papers3", NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return true;
    if (e != ESP_OK) return false;
    size_t size = 0;
    e = nvs_get_blob(h, key, nullptr, &size);
    if (e == ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return true; }
    if (e != ESP_OK || size == 0 || size > 65536) { nvs_close(h); return false; }
    out.resize(size);
    e = nvs_get_blob(h, key, out.data(), &size);
    nvs_close(h);
    return e == ESP_OK;
}
bool write_record(const char *key, const std::string &value) {
    nvs_handle_t h;
    if (nvs_open("papers3", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = value.empty() ? nvs_erase_key(h, key)
                              : nvs_set_blob(h, key, value.data(), value.size());
    if (e == ESP_ERR_NVS_NOT_FOUND && value.empty()) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}
}

bool papers3_load_invoice(PaperInvoice &invoice) {
    invoice = {};
    std::string value;
    if (!read_record("invoice", value)) return false;
    if (value.empty()) return true;
    cJSON *j = cJSON_Parse(value.c_str());
    if (!j) return false;
    const auto *slot = cJSON_GetObjectItemCaseSensitive(j, "slot");
    const auto *amount = cJSON_GetObjectItemCaseSensitive(j, "amount");
    const auto *mint = cJSON_GetObjectItemCaseSensitive(j, "mint");
    const auto *id = cJSON_GetObjectItemCaseSensitive(j, "id");
    const auto *request = cJSON_GetObjectItemCaseSensitive(j, "request");
    bool ok = cJSON_IsNumber(slot) && slot->valuedouble >= 0 &&
              slot->valuedouble < MAX_MINTS && floor(slot->valuedouble) == slot->valuedouble &&
              cJSON_IsNumber(amount) && amount->valuedouble > 0 &&
              amount->valuedouble <= INT_MAX && floor(amount->valuedouble) == amount->valuedouble &&
              cJSON_IsString(mint) && cJSON_IsString(id) && cJSON_IsString(request);
    if (ok) {
        invoice.slot = slot->valueint;
        invoice.amount = amount->valueint;
        invoice.mint = mint->valuestring;
        invoice.quote.quote = id->valuestring;
        invoice.quote.request = request->valuestring;
        invoice.quote.unit = "sat";
        invoice.quote.method = "bolt11";
        invoice.quote.amount = invoice.amount;
        ok = !invoice.mint.empty() && !invoice.quote.quote.empty() && !invoice.quote.request.empty();
    }
    cJSON_Delete(j);
    return ok;
}

bool papers3_save_invoice(const PaperInvoice &invoice) {
    cJSON *j = cJSON_CreateObject();
    if (!j) return false;
    bool ok = cJSON_AddNumberToObject(j, "slot", invoice.slot) &&
              cJSON_AddNumberToObject(j, "amount", invoice.amount) &&
              cJSON_AddStringToObject(j, "mint", invoice.mint.c_str()) &&
              cJSON_AddStringToObject(j, "id", invoice.quote.quote.c_str()) &&
              cJSON_AddStringToObject(j, "request", invoice.quote.request.c_str());
    char *raw = ok ? cJSON_PrintUnformatted(j) : nullptr;
    ok = raw && write_record("invoice", raw);
    cJSON_free(raw);
    cJSON_Delete(j);
    return ok;
}
bool papers3_clear_invoice() { return write_record("invoice", ""); }
bool papers3_load_outbox(std::string &token) { return read_record("outbox", token); }
bool papers3_clear_outbox() { return write_record("outbox", ""); }

bool papers3_save_outbox(const cashu::Token &token) {
    std::string existing;
    if (!papers3_load_outbox(existing) || !existing.empty()) return false;
    auto *wallet = wallet_store_find(token.mint.c_str());
    if (!wallet || token.proofs.empty()) return false;
    std::string serialized = cashu::serialize_token_v4(token);
    if (serialized.empty() || !write_record("outbox", serialized)) return false;
    // Journal first, then debit. Boot repeats this idempotent debit before
    // enabling either console or UI, covering power loss between the commits.
    return wallet->remove_proofs(token.proofs);
}

bool papers3_storage_recover() {
    wallet_store_guard guard;
    PaperInvoice invoice;
    if (!papers3_load_invoice(invoice)) return false;
    if (invoice.slot >= 0) {
        auto *w = wallet_store_get(invoice.slot);
        if (!w || w->mint_url() != invoice.mint) return false;
    }
    std::string raw;
    if (!papers3_load_outbox(raw)) return false;
    if (raw.empty()) return true;
    cashu::Token token;
    if (!cashu::deserialize_token(raw.c_str(), token) || token.proofs.empty()) return false;
    auto *wallet = wallet_store_find(token.mint.c_str());
    return wallet && wallet->remove_proofs(token.proofs);
}

