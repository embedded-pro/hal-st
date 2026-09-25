#ifndef HAL_ST_BLE_STACK_PROCESS_WBA_HPP
#define HAL_ST_BLE_STACK_PROCESS_WBA_HPP

namespace hal
{
    // Lets the link layer deliver events again after the host refused one, and runs the host stack to take them
    void ResumeBleEventFlow();
}

#endif
