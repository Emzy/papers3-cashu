#include "commands.h"
#include "console.h"
#include "wallet.hpp"
#include "wallet_store.hpp"
#include "cashu.hpp"
#include "cashu_json.hpp"
#include "cashu_cbor.hpp"
#include "keyset.hpp"
#include "unit.hpp"
#include "wifi.h"
#include "nfc.hpp"
#include "display.h"
#include "ui.h"
#include "papers3.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "nucula"

// Money commands: status/balance/unit plus the receive, mint, invoice,
// claim, melt, and stickup flows.

static void cmd_status(const char *arg)
{
    wallet_store_guard guard;
    (void)arg;
    console_printf("wifi:    %s\r\n", wifi_is_connected() ? "connected" : "disconnected");
    console_printf("nfc:     %s\r\n", nfc_status_str());
    console_printf("heap:    %lu bytes free\r\n",
                   (unsigned long)esp_get_free_heap_size());
    console_printf("unit:    %s (default)\r\n",
                   cashu::Wallet::default_unit().c_str());

    int count = wallet_store_count();
    console_printf("mints:   %d/%d\r\n", count, MAX_MINTS);

    for (int i = 0; i < MAX_MINTS; i++) {
        auto *w = wallet_store_get(i);
        if (!w) continue;
        console_printf("[%d] %s\r\n", i, w->mint_url().c_str());
        for (const auto &ks : w->keysets())
            if (ks.active)
                console_printf("    active:  %s (%s, %d keys)\r\n",
                               ks.id.c_str(), ks.unit.c_str(),
                               (int)ks.keys.size());
        console_printf("    keysets: %d\r\n", (int)w->keysets().size());
        console_printf("    proofs:  %d\r\n", (int)w->proofs().size());
        if (print_unit_balances(w, "    ", "balance: "))
            console_print("    warning: proofs with unknown keyset\r\n");
    }
    if (count > 0) {
        std::vector<std::string> units;
        wallet_store_collect_units(units);
        for (const auto &u : units) {
            if (u == "?") continue;
            char amt[48];
            cashu::format_amount(amt, sizeof(amt),
                                 wallet_store_balance_for_unit(u.c_str()),
                                 u.c_str());
            console_printf("total:   %s\r\n", amt);
        }
    }
}

static void cmd_balance(const char *arg)
{
    wallet_store_guard guard;
    (void)arg;
    bool any = false;
    for (int i = 0; i < MAX_MINTS; i++) {
        auto *w = wallet_store_get(i);
        if (!w || w->proofs().empty()) continue;
        any = true;
        console_printf("[%s]\r\n", w->mint_url().c_str());
        for (const auto &p : w->proofs()) {
            const std::string *u = w->unit_for_proof(p);
            char amt[48];
            cashu::format_amount(amt, sizeof(amt), p.amount,
                                 u ? u->c_str() : "?");
            console_printf("  %s  (keyset %s)\r\n", amt, p.id.c_str());
        }
        if (print_unit_balances(w, "  ", "subtotal: "))
            console_print("  warning: proofs with unknown keyset excluded\r\n");
    }
    if (!any) {
        console_print("no proofs\r\n");
        return;
    }
    std::vector<std::string> units;
    wallet_store_collect_units(units);
    for (const auto &u : units) {
        if (u == "?") continue;
        char amt[48];
        cashu::format_amount(amt, sizeof(amt),
                             wallet_store_balance_for_unit(u.c_str()), u.c_str());
        console_printf("total: %s\r\n", amt);
    }
}

// unit [<name>] — show or set the persisted default unit.
static void cmd_unit(const char *arg)
{
    wallet_store_guard guard;
    if (!arg || strlen(arg) == 0) {
        console_printf("default unit: %s\r\n",
                       cashu::Wallet::default_unit().c_str());
        std::vector<std::string> units;
        wallet_store_collect_units(units);
        if (!units.empty()) {
            console_print("held units:");
            for (const auto &u : units)
                console_printf(" %s", u.c_str());
            console_print("\r\n");
        }
        return;
    }

    // First token only; normalize before validating.
    const char *end = strchr(arg, ' ');
    std::string unit = cashu::normalize_unit(
        end ? std::string(arg, end - arg) : std::string(arg));
    if (!cashu::unit_token_valid(unit.c_str())) {
        console_print("error: unit must be non-empty [a-z0-9_-]\r\n");
        return;
    }

    // Warn — but allow — when no mint currently backs the unit; keysets
    // may simply not be fetched yet.
    bool backed = false;
    for (int i = 0; i < MAX_MINTS && !backed; i++) {
        auto *w = wallet_store_get(i);
        if (w && w->active_keyset(unit))
            backed = true;
    }

    if (!cashu::Wallet::set_default_unit(unit)) {
        console_print("error: failed to persist default unit\r\n");
        return;
    }
    console_printf("default unit: %s%s\r\n", unit.c_str(),
                   backed ? "" : " (no mint has an active keyset for it yet)");
    ui_refresh();
}

static void cmd_receive(const char *arg)
{
    wallet_store_guard guard;
    if (!arg || strlen(arg) == 0) {
        console_print("usage: receive <cashu token>\r\n");
        return;
    }
    if (!wifi_is_connected()) {
        console_print("error: not connected to wifi\r\n");
        return;
    }

    cashu::Token token;
    if (!cashu::deserialize_token(arg, token)) {
        console_print("error: failed to decode token\r\n");
        return;
    }

    if (token.mint.compare(0, 8, "https://") != 0) {
        console_print("error: token mint must use HTTPS\r\n"); return;
    }
    char amt[48];
    cashu::format_amount(amt, sizeof(amt),
                         cashu::proofs_sum(token.proofs), token.unit.c_str());
    console_printf("token: %s in %d proofs from %s\r\n",
                   amt, (int)token.proofs.size(), token.mint.c_str());

    cashu::Wallet *w = wallet_store_get_or_create(token.mint);
    if (!w) {
        console_printf("error: max %d mints, remove one first\r\n", MAX_MINTS);
        return;
    }

    if (!ensure_active_keyset(w, token.unit, /*require_active=*/false))
        return;

    console_print("swapping...\r\n");
    std::vector<cashu::Proof> received;
    int64_t t0 = esp_timer_get_time();
    if (!w->receive(token, received)) {
        print_flow_error(w, "receive failed");
        return;
    }
    long long ms = (esp_timer_get_time() - t0) / 1000;

    cashu::format_amount(amt, sizeof(amt),
                         cashu::proofs_sum(received), token.unit.c_str());
    console_printf("received %s in %d proofs (%lld ms)\r\n",
                   amt, (int)received.size(), ms);
    ui_refresh();
}

static void cmd_mint(const char *arg)
{
    wallet_store_guard guard;
    if (!arg || strlen(arg) == 0 || strcmp(arg, "list") == 0) {
        int count = wallet_store_count();
        if (count == 0) {
            console_print("no mints configured\r\n");
            return;
        }
        for (int i = 0; i < MAX_MINTS; i++) {
            if (!wallet_store_get(i)) continue;
            console_printf("[%d] %s  (%d keysets, %lld sat)\r\n",
                           i, wallet_store_get(i)->mint_url().c_str(),
                           (int)wallet_store_get(i)->keysets().size(),
                           (long long)wallet_store_get(i)->balance());
        }
        console_printf("%d/%d slots used\r\n", count, MAX_MINTS);
        return;
    }

    if (strncmp(arg, "add ", 4) == 0) {
        const char *url = arg + 4;
        while (*url == ' ') url++;
        if (strlen(url) == 0) {
            console_print("usage: mint add <url>\r\n");
            return;
        }
        if (strncmp(url, "https://", 8) != 0) {
            console_print("error: mint must use https://\r\n"); return;
        }
        if (wallet_store_find(url)) {
            console_print("mint already added\r\n");
            return;
        }
        auto *w = wallet_store_get_or_create(url);
        if (!w) {
            console_printf("error: max %d mints, remove one first\r\n", MAX_MINTS);
            return;
        }
        console_printf("added mint [%d]: %s\r\n", w->nvs_slot(), url);

        if (wifi_is_connected()) {
            console_print("loading keysets...\r\n");
            if (!w->load_keysets())
                console_print("warning: failed to load keysets\r\n");
        } else {
            console_print("offline: keysets will load when connected\r\n");
        }
        ui_refresh();
        return;
    }

    if (strncmp(arg, "info", 4) == 0 && (arg[4] == '\0' || arg[4] == ' ')) {
        const char *idx = arg + 4;
        while (*idx == ' ') idx++;
        cashu::Wallet *w = resolve_wallet(*idx ? idx : nullptr);
        if (!w) return;
        if (!w->mint_info()) {
            if (!wifi_is_connected()) {
                console_print("error: not connected to wifi\r\n");
                return;
            }
            console_print("loading mint info...\r\n");
            if (!w->load_mint_info()) {
                console_print("error: failed to load mint info\r\n");
                return;
            }
        }
        const cashu::MintInfo *info = w->mint_info();
        console_printf("%s\r\n", info->name.empty() ? w->mint_url().c_str()
                                                    : info->name.c_str());
        auto print_rows = [](const char *label,
                             const std::vector<cashu::MintMethodSetting> &rows) {
            console_printf("%s\r\n", label);
            if (rows.empty()) {
                console_print("  (none advertised)\r\n");
                return;
            }
            for (const auto &r : rows) {
                char line[96];
                int n = snprintf(line, sizeof(line), "  %s %s",
                                 r.method.c_str(), r.unit.c_str());
                if (r.method_name && n > 0 && n < (int)sizeof(line))
                    n += snprintf(line + n, sizeof(line) - n, " \"%s\"",
                                  r.method_name->c_str());
                if ((r.min_amount || r.max_amount) && n > 0 && n < (int)sizeof(line))
                    snprintf(line + n, sizeof(line) - n, " (%lld..%lld)",
                             r.min_amount ? (long long)*r.min_amount : 0LL,
                             r.max_amount ? (long long)*r.max_amount : 0LL);
                console_printf("%s\r\n", line);
            }
        };
        print_rows("mint:", info->mint_methods);
        print_rows("melt:", info->melt_methods);
        return;
    }

    if (strncmp(arg, "remove ", 7) == 0) {
        console_print("Mint removal is disabled in this experimental port to preserve saved payment records.\r\n"); return;
    }

    console_print("usage: mint [list|add <url>|remove <index|url>|info [idx]]\r\n");
}

static void cmd_invoice(const char *arg)
{
    wallet_store_guard guard;
    PaperInvoice saved;
    if (!papers3_load_invoice(saved)) {
        console_print("error: saved invoice is unreadable\r\n"); return;
    }
    if (arg && strcmp(arg, "show") == 0) {
        if (saved.slot < 0) { console_print("no saved invoice\r\n"); return; }
        console_print(saved.quote.request.c_str()); console_print("\r\nquote: ");
        console_print(saved.quote.quote.c_str()); console_print("\r\n"); return;
    }
    if (arg && strcmp(arg, "discard") == 0) {
        console_print(papers3_clear_invoice() ? "saved invoice discarded\r\n" : "error: discard failed\r\n");
        ui_refresh(); return;
    }
    if (saved.slot >= 0) {
        console_print("error: an invoice is already saved; use Receive, invoice show, or invoice discard\r\n"); return;
    }
    if (!arg || strlen(arg) == 0) {
        console_print("usage: invoice <amount> [u=<unit>] [m=<method>] [w=<mint_idx>]\r\n");
        return;
    }
    if (!wifi_is_connected()) {
        console_print("error: not connected to wifi\r\n");
        return;
    }

    char *amount_end = nullptr;
    long parsed = strtol(arg, &amount_end, 10);
    if (amount_end == arg || (*amount_end && *amount_end != ' ') || parsed > 9999999 || parsed <= 0) {
        console_print("error: amount must be an integer from 1 to 9999999 sats\r\n"); return;
    }
    int amount = static_cast<int>(parsed);
    if (amount <= 0) {
        console_print("error: amount must be positive\r\n");
        return;
    }

    CmdOpts opts;
    if (!parse_cmd_opts(strchr(arg, ' '), opts))
        return;
    const std::string unit = opts.unit.empty() ? cashu::Wallet::default_unit()
                                               : opts.unit;
    const std::string method = opts.method.empty() ? std::string("bolt11")
                                                   : opts.method;

    if (unit != "sat" || method != "bolt11") {
        console_print("PaperS3 invoice UI supports sat/bolt11 only\r\n"); return;
    }

    cashu::Wallet *w = resolve_wallet(opts.idx.empty() ? nullptr
                                                       : opts.idx.c_str());
    if (!w) return;

    if (!ensure_active_keyset(w, unit, /*require_active=*/true))
        return;

    console_print("requesting mint quote...\r\n");
    cashu::MintQuote quote;
    if (!w->request_mint_quote(amount, unit, method, quote)) {
        print_flow_error(w, "failed to get mint quote");
        return;
    }

    if (!papers3_show_invoice(w->nvs_slot(), amount, quote)) {
        console_print("error: invoice was not displayed; do not pay it; check invoice show\r\n"); return;
    }

    // For bolt11 `request` is an invoice; for custom methods it is an
    // opaque payment target (URL, account reference) — print verbatim.
    console_print(method == "bolt11" ? "pay this invoice:\r\n"
                                            : "payment request:\r\n");
    console_print(quote.request.c_str());
    console_print("\r\n");
    console_printf("quote: %s\r\n", quote.quote.c_str());
    char amt[48];
    cashu::format_amount(amt, sizeof(amt), quote.amount, quote.unit.c_str());
    console_printf("amount: %s\r\n", amt);
    if (method == "bolt11")
        console_print("then run: claim <quote_id>\r\n");
    else
        console_printf("then run: claim %s m=%s\r\n",
                       quote.quote.c_str(), method.c_str());
}

static void cmd_claim(const char *arg)
{
    wallet_store_guard guard;
    if (!arg || strlen(arg) == 0) {
        console_print("usage: claim <quote_id> [m=<method>] [w=<mint_idx>]\r\n");
        return;
    }
    if (!wifi_is_connected()) {
        console_print("error: not connected to wifi\r\n");
        return;
    }

    std::string quote_id;
    CmdOpts opts;
    if (!split_first_token(arg, quote_id, opts))
        return;
    const std::string method = opts.method.empty() ? std::string("bolt11")
                                                   : opts.method;

    cashu::Wallet *w = nullptr;
    cashu::MintQuote quote;

    if (!opts.idx.empty()) {
        w = resolve_wallet(opts.idx.c_str());
        if (!w) return;
        if (!w->check_mint_quote(quote_id, method, quote)) {
            console_print("error: quote not found on this mint\r\n");
            return;
        }
    } else {
        for (int i = 0; i < MAX_MINTS; i++) {
            if (!wallet_store_get(i)) continue;
            if (wallet_store_get(i)->check_mint_quote(quote_id, method, quote)) {
                w = wallet_store_get(i);
                break;
            }
        }
    }

    if (!w) {
        console_print("error: quote not found on any mint\r\n");
        return;
    }

    // Claimable = amount_paid - amount_issued on current mints; legacy
    // bolt11 mints only expose state, where PAID means the full amount.
    PaperInvoice saved;
    const bool saved_match = papers3_load_invoice(saved) && saved.slot == w->nvs_slot()
        && saved.quote.quote == quote_id && saved.mint == w->mint_url();
    if (saved_match && quote.amount <= 0) quote.amount = saved.amount;
    int claimable = quote.mintable();
    if (claimable <= 0) {
        if (quote.state == "UNPAID")
            console_print("invoice not paid yet\r\n");
        else if (quote.state == "ISSUED")
            console_print("tokens already claimed\r\n");
        else if (quote.amount_paid && quote.amount_issued)
            console_print("nothing mintable (paid amount fully issued)\r\n");
        else
            console_printf("unexpected state: %s\r\n", quote.state.c_str());
        return;
    }

    const std::string unit = quote.unit.empty() ? std::string("sat") : quote.unit;
    if (!ensure_active_keyset(w, unit, /*require_active=*/true))
        return;

    console_print("minting tokens...\r\n");
    if (!w->mint_tokens(quote, claimable)) {
        print_flow_error(w, "minting failed");
        return;
    }

    char amt[48];
    cashu::format_amount(amt, sizeof(amt), claimable,
                         quote.unit.empty() ? "sat" : quote.unit.c_str());
    console_printf("minted %s\r\n", amt);
    if (saved_match && !papers3_clear_invoice()) console_print("warning: saved invoice cleanup failed\r\n");
    ui_refresh();
}

static void cmd_melt(const char *arg)
{
    wallet_store_guard guard;
    if (!arg || strlen(arg) == 0) {
        console_print("usage: melt <request> [u=<unit>] [m=<method>] "
                             "[a=<amount>] [w=<mint_idx>]\r\n");
        return;
    }
    if (!wifi_is_connected()) {
        console_print("error: not connected to wifi\r\n");
        return;
    }

    std::string request;
    CmdOpts opts;
    if (!split_first_token(arg, request, opts))
        return;
    const std::string unit = opts.unit.empty() ? cashu::Wallet::default_unit()
                                               : opts.unit;
    const std::string method = opts.method.empty() ? std::string("bolt11")
                                                   : opts.method;

    if (unit != "sat" || method != "bolt11") {
        console_print("PaperS3 payment confirmation supports sat/bolt11 only\r\n"); return;
    }

    cashu::Wallet *w = resolve_wallet(opts.idx.empty() ? nullptr
                                                       : opts.idx.c_str());
    if (!w) return;

    if (!ensure_active_keyset(w, unit, /*require_active=*/true))
        return;

    console_print("requesting melt quote...\r\n");
    cashu::MeltQuote quote;
    std::optional<int> req_amount = opts.amount > 0
        ? std::optional<int>(opts.amount) : std::nullopt;
    if (!w->request_melt_quote(request, unit, method, quote, req_amount)) {
        print_flow_error(w, "failed to get melt quote");
        return;
    }

    long long wallet_bal = w->balance_for_unit(quote.unit);
    int64_t total_needed = int64_t(quote.amount) + quote.fee_reserve;
    char amt[48];
    cashu::format_amount(amt, sizeof(amt), quote.amount, quote.unit.c_str());
    console_printf("amount:      %s\r\n", amt);
    cashu::format_amount(amt, sizeof(amt), quote.fee_reserve, quote.unit.c_str());
    console_printf("fee_reserve: %s\r\n", amt);
    cashu::format_amount(amt, sizeof(amt), wallet_bal, quote.unit.c_str());
    console_printf("balance:     %s\r\n", amt);

    if (wallet_bal < total_needed) {
        console_printf("error: insufficient %s balance (%lld < %lld)\r\n",
                       quote.unit.c_str(), wallet_bal, (long long)total_needed);
        return;
    }

    if (!papers3_stage_melt(w->nvs_slot(), quote, request)) {
        console_print("error: another payment is waiting for confirmation\r\n"); return;
    }
    console_print("Review and confirm on the PaperS3 touchscreen.\r\n");
}


static void cmd_stickup(const char *)
{
    console_print("Use Send ecash on the touchscreen; exports are saved in the outbox.\r\n");
}

void commands_wallet_register(void)
{
    console_register_cmd("status",  cmd_status,  "show system and wallet status");
    console_register_cmd("balance", cmd_balance,  "show wallet balance per unit");
    console_register_cmd("unit",    cmd_unit,     "unit [<name>] — show/set default unit");
    console_register_cmd("receive", cmd_receive,  "receive a cashuA/cashuB token");
    console_register_cmd("mint",    cmd_mint,     "mint [list|add <url>|info]");
    console_register_cmd("invoice", cmd_invoice,  "invoice <sats> [w=] | show | discard");
    console_register_cmd("claim",   cmd_claim,    "claim <quote_id> [m=|w=]");
    console_register_cmd("melt",    cmd_melt,     "melt <bolt11> [w=] -- confirm on device");
    console_register_cmd("stickup", cmd_stickup,  "use touchscreen Send ecash instead");
}
