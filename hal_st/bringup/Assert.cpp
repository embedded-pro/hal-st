#include <cstdint>
#include <cstdlib>

extern "C"
{
    void __assert_func(const char*, int, const char*, const char*)
    {
        std::abort();
    }

    void assert_failed(uint8_t* file, uint32_t line)
    {
        std::abort();
    }
}
