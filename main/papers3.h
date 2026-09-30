#pragma once
#include "cashu.hpp"
#include <string>

void papers3_display_init();
void papers3_fatal(const char *title, const char *detail);
void papers3_start();
bool papers3_stage_melt(int slot, const cashu::MeltQuote &quote,
                       const std::string &request);
bool papers3_show_invoice(int slot, int amount, const cashu::MintQuote &quote);

struct PaperInvoice {
    int slot = -1;
    int amount = 0;
    std::string mint;
    cashu::MintQuote quote;
};

// The caller holds wallet_store_guard. Records are single, committed NVS blobs.
bool papers3_storage_recover();
bool papers3_load_invoice(PaperInvoice &invoice);
bool papers3_save_invoice(const PaperInvoice &invoice);
bool papers3_clear_invoice();
bool papers3_load_outbox(std::string &token);
bool papers3_save_outbox(const cashu::Token &token);
bool papers3_clear_outbox();

