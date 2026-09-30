#pragma once
#include "cashu.hpp"
#include <algorithm>
#include <set>

// Snapshot the available sat proofs when opening the selector. Never select
// by an index into the live wallet: a USB redemption can reorder that vector.
class PaperTokenSelection {
public:
    void reset(std::vector<cashu::Proof> proofs) {
        proofs_ = std::move(proofs);
        std::stable_sort(proofs_.begin(), proofs_.end(), [](const auto &a, const auto &b) {
            return a.amount > b.amount;
        });
        selected_.assign(proofs_.size(), false);
    }
    size_t size() const { return proofs_.size(); }
    const cashu::Proof &proof(size_t index) const { return proofs_.at(index); }
    bool selected(size_t index) const { return index < selected_.size() && selected_[index]; }
    void toggle(size_t index) { if (index < selected_.size()) selected_[index] = !selected_[index]; }
    std::vector<cashu::Proof> chosen() const {
        std::vector<cashu::Proof> result;
        for (size_t i = 0; i < proofs_.size(); ++i)
            if (selected_[i]) result.push_back(proofs_[i]);
        return result;
    }
    size_t count() const { return std::count(selected_.begin(), selected_.end(), true); }
    int64_t amount() const {
        int64_t result = 0;
        for (size_t i = 0; i < proofs_.size(); ++i)
            if (selected_[i]) result += proofs_[i].amount;
        return result;
    }
private:
    std::vector<cashu::Proof> proofs_;
    std::vector<bool> selected_;
};

inline bool paper_selection_available(const std::vector<cashu::Proof> &available,
                                      const std::vector<cashu::Proof> &chosen) {
    if (chosen.empty()) return false;
    std::set<std::string> secrets;
    for (const auto &proof : chosen) {
        if (proof.amount <= 0 || !secrets.insert(proof.secret).second) return false;
        const auto matches = std::count_if(available.begin(), available.end(), [&](const auto &p) {
            return p.secret == proof.secret;
        });
        if (matches != 1) return false;
        auto live = std::find_if(available.begin(), available.end(), [&](const auto &p) {
            return p.secret == proof.secret && p.id == proof.id &&
                   p.amount == proof.amount && p.C == proof.C;
        });
        if (live == available.end()) return false;
    }
    return true;
}

// Each QR is a complete, independently redeemable Cashu token. Keep the full
// proof, including DLEQ and witness, rather than trimming cryptographic data.
inline bool paper_token_page(const cashu::Token &saved, size_t index, cashu::Token &page) {
    page = {};
    if (index >= saved.proofs.size()) return false;
    page.mint = saved.mint;
    page.unit = saved.unit;
    page.memo = saved.memo;
    page.proofs.push_back(saved.proofs[index]);
    return true;
}
