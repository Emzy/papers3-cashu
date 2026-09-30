#include "nfc.hpp"
#include "keypad.h"

// PaperS3 has neither the reference board's PN7160 nor its keypad.
// In particular, never initialize their GPIOs: they overlap the e-paper bus.
bool nfc_init(i2c_master_bus_handle_t) { return false; }
bool nfc_request_start(int, const char *, const char *) { return false; }
void nfc_request_stop() {}
NfcState nfc_state() { return NfcState::off; }
const char *nfc_status_str() { return "not fitted (PaperS3)"; }
char keypad_wait_event(uint32_t) { return 0; }
