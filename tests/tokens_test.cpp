#include "papers3_tokens.hpp"
#include "papers3_qr.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <climits>

static cashu::Proof proof(int amount, const std::string &secret) {
    cashu::Proof p{"00aabbccddeeff00", amount, secret, "signature", {}, {}};
    p.dleq = cashu::DLEQ{"e", "s", "r"};
    p.witness = "witness";
    return p;
}

int main(int argc, char **argv) {
    const std::vector<cashu::Proof> available = {proof(2,"a"), proof(8,"b"), proof(2,"c"), proof(1,"d"), proof(16,"e")};
    PaperTokenSelection selection;
    selection.reset(available);
    assert(selection.count() == 0 && selection.amount() == 0);
    assert(selection.proof(0).amount == 16 && selection.proof(1).amount == 8);
    selection.toggle(0); selection.toggle(3);
    assert(selection.count() == 2 && selection.amount() == 18);
    auto chosen = selection.chosen();
    assert(chosen[0].secret == "e" && chosen[1].secret == "c");
    assert(paper_selection_available(available, chosen));
    auto changed = available;
    std::reverse(changed.begin(), changed.end()); changed.push_back(proof(4,"new"));
    assert(paper_selection_available(changed, chosen)); // unrelated changes are safe
    changed = available; changed.pop_back();
    assert(!paper_selection_available(changed, chosen)); // selected proof spent meanwhile
    changed = available; changed.back().amount = 32;
    assert(!paper_selection_available(changed, chosen));
    changed = available; changed.back().C = "changed-signature";
    assert(!paper_selection_available(changed, chosen));
    changed = available; changed.push_back(changed.back());
    assert(!paper_selection_available(changed, chosen)); // ambiguous duplicate secret
    auto duplicate = chosen; duplicate.push_back(chosen[0]);
    assert(!paper_selection_available(available, duplicate));
    assert(!paper_selection_available(available, {}));
    selection.toggle(3); selection.toggle(999);
    assert(selection.count() == 1 && selection.amount() == 16);
    selection.reset({proof(INT_MAX,"a"),proof(INT_MAX,"b")});
    selection.toggle(0); selection.toggle(1);
    assert(selection.amount() == int64_t(INT_MAX) * 2);

    cashu::Token token;
    token.mint = "https://mint.example"; token.unit = "sat";
    token.memo = "test"; token.proofs = chosen;
    cashu::Token page;
    int64_t total = 0;
    for (size_t i = 0; i < token.proofs.size(); ++i) {
        assert(paper_token_page(token, i, page));
        assert(page.proofs.size() == 1 && page.proofs[0].secret == token.proofs[i].secret);
        assert(page.mint == token.mint && page.unit == token.unit && page.memo == token.memo);
        assert(page.proofs[0].dleq->r == "r" && page.proofs[0].witness == "witness");
        total += page.proofs[0].amount;
    }
    assert(total == cashu::proofs_sum(token.proofs));
    assert(!paper_token_page(token, 999, page) && page.proofs.empty());

    PaperQR qr;
    assert(!qr.encode(""));
    assert(!qr.encode(std::string(1801,'x')));
    assert(!qr.encode(std::string(1700,'x'))); // must not shrink the pixels to fit
    for (const auto &payload : {std::string("cashuBtest"), std::string(450,'x'), std::string(650,'x')}) {
        assert(qr.encode(payload));
        assert(qr.scale >= 5 && qr.offset >= 4 * qr.scale);
        assert(qr.offset + qr.code.size * qr.scale + 4 * qr.scale <= 520);
        // Check a known QR finder pattern through the actual C/C++ interface.
        // This catches non-normalized bitmask values returned as native bool.
        for (int y = 0; y < 7; ++y) for (int x = 0; x < 7; ++x) {
            const bool expected = x == 0 || x == 6 || y == 0 || y == 6 ||
                                  (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            const bool actual = lgfx_qrcode_getModule(&qr.code, x, y);
            assert(actual == expected);
        }
    }
    if (argc == 3) {
        std::ifstream input(argv[1]);
        const std::string payload((std::istreambuf_iterator<char>(input)), {});
        assert(qr.encode(payload));
        std::ofstream pgm(argv[2], std::ios::binary);
        pgm << "P5\n520 520\n255\n";
        for (int y = 0; y < 520; ++y) for (int x = 0; x < 520; ++x) {
            int col = (x - qr.offset) / qr.scale, row = (y - qr.offset) / qr.scale;
            bool dark = x >= qr.offset && y >= qr.offset && col < qr.code.size && row < qr.code.size &&
                        lgfx_qrcode_getModule(&qr.code, col, row);
            pgm.put(dark ? '\0' : char(255));
        }
        std::cout << "QR fixture: " << payload.size() << " characters, " << int(qr.code.size)
                  << " modules, " << qr.scale << " pixels/module\n";
    }
    std::cout << "PASS: token selection, stale-proof rejection, independent pages, metadata preservation, QR size and quiet zone\n";
}
