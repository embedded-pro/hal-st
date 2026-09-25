/* ST's ACI/HCI wrappers, with the host stack run after every command so that the procedures it starts make progress */

void BleStackCB_Process(void);

#define BLE_WRAP_POSTPROC BleStackCB_Process

#include "auto/ble_wrap.c"
