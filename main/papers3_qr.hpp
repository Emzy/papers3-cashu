#pragma once
#include <lgfx/utility/lgfx_qrcode.h>
#include <string>
#include <vector>

struct PaperQR {
    static constexpr int quiet_modules = 4;
    static constexpr int min_scale = 5;
    QRCode code{};
    std::vector<uint8_t> modules;
    int scale = 0;
    int offset = 0;

    bool encode(const std::string &payload, int box_size = 520) {
        scale = offset = 0;
        code = {};
        modules.clear();
        if (payload.empty() || payload.size() > 1800) return false;
        for (uint8_t version = 1; version <= 40; ++version) {
            const int side = 17 + 4 * version;
            if (box_size / (side + 2 * quiet_modules) < min_scale) break;
            modules.resize(lgfx_qrcode_getBufferSize(version));
            if (lgfx_qrcode_initText(&code, modules.data(), version, ECC_LOW, payload.c_str()) != 0) continue;
            scale = box_size / (code.size + 2 * quiet_modules);
            offset = (box_size - code.size * scale) / 2;
            return true;
        }
        code = {};
        modules.clear();
        return false;
    }
};
