MEMORY
{
    FLASH (rx)              : ORIGIN = 0x08100000, LENGTH = 1024K
    RAM_SHARED (xrw)        : ORIGIN = 0, LENGTH = 0
    RAM (xrw)               : ORIGIN = 0x30000000, LENGTH = 288K
}
