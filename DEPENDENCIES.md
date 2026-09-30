# Pinned source provenance

| Component | Source | Revision |
|---|---|---|
| Wallet base | https://github.com/zeugmaster/nucula | `8ad081219c74f3e8372551738da37a08834835a9` |
| libsecp256k1 | https://github.com/bitcoin-core/secp256k1 | `ac561601b8a3452bc1869746de423359944d4e00` |
| M5Unified | https://github.com/m5stack/M5Unified | `44d0c52dca65b5bf8e49fd75dde833ee800754f4` |
| M5GFX | https://github.com/m5stack/M5GFX | `cd363dde6362ab3f9419661cf3ab7a6b4f06f620` |
| CBOR IDF wrapper | https://github.com/espressif/idf-extra-components | `126f42ecb2a1344e84edfd688872e0941425fad9` (`cbor/`) |
| tinycbor | https://github.com/intel/tinycbor | `6442e749ca811e24afad1551338a45c75e09808f` |
| ESP-IDF toolchain/framework | https://github.com/espressif/esp-idf | `v5.5.1`, commit `fcae32885b0296b32044cb99ecbdc50d98dddb83` |
| Host-test cJSON | https://github.com/DaveGamble/cJSON | `8f2beb57ddad1f94bed899790b00f46df893ccac` (IDF submodule) |

Libraries are included with their existing license notices. The M5Unified component manifest was adjusted to use the vendored sibling M5GFX instead of resolving another managed copy. Example/documentation folders and Git metadata were omitted from vendored display libraries; their build source is unchanged except for the QR fix below.

**Upstream wallet licensing needs clarification before publication.** The downloaded Nucula snapshot does not contain a root license file or an explicit license grant in its README. Public source availability alone does not establish redistribution terms. This local experimental adaptation does not assign a license to upstream code. Obtain the author's terms before publishing or distributing a derived wallet/firmware. Third-party component licenses do not resolve that issue.

Hardware references: [PaperS3 documentation](https://docs.m5stack.com/en/core/PaperS3), [M5Stack factory source](https://github.com/m5stack/M5PaperS3-UserDemo), and the pinned M5 driver source. Cashu protocol: [Cashu NUTs](https://github.com/cashubtc/nuts).

## Local QR patch in 0.2.0

`components/M5GFX/src/lgfx/utility/lgfx_qrcode.c`: normalize `lgfx_qrcode_getModule()` to return 0 or 1. On pre-C23 C builds the header typedefs `bool` as `unsigned char`, while C++ callers use native `bool`; returning a raw bitmask breaks that boundary on the host compiler. The patch preserves the encoded QR data and fixes pixel rendering. A host finder-pattern regression check exercises the same C/C++ interface.
