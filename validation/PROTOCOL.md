# Validation terminal protocol

The validation firmware (`validation/firmware`) exposes the hal-st peripherals of a Nucleo board through EMIL's hardware-in-the-loop terminal (`services::HilTerminal` and the command groups of `services.hil.commands`); hal-st supplies the board profiles, the STM32 pin factory and one factory per peripheral.
The host package (`validation/host`) drives this terminal and a Digilent Analog Discovery 3 to validate the peripherals.

## Board profiles

Two boards are supported: NUCLEO-WB55RG (`TARGET_MCU` `stm32wb55`) and NUCLEO-WBA55CG (`TARGET_MCU` `stm32wba55`).
The firmware runs from the default Nucleo clocks of hal-st (`ConfigureDefaultClockNucleoWB55RG`, 64 MHz from the HSI PLL; `ConfigureDefaultClockNucleoWBA55CG`, 100 MHz from the 32 MHz HSE PLL), with every APB prescaler at 1.
The terminal is a `hal::UartStmDuplexDma` on USART1, the ST-LINK virtual COM port, at 921600 8N1 without flow control.

Aliases name the pins by peripheral function:

| Alias                                         | NUCLEO-WB55RG           | NUCLEO-WBA55CG          | Function                                                         |
|-----------------------------------------------|-------------------------|-------------------------|------------------------------------------------------------------|
| `terminaltx` `terminalrx`                     | PB6 PB7                 | PB12 PA8                | terminal USART1 TX / RX                                          |
| `ain1` `ain2` `ain3` `ain4` `ain5` `ain6`     | PC0 PC1 PC2 PC3 PA0 PA1 | -                       | ADC1 inputs IN1-IN6                                              |
| `ain2` `ain3` `ain4` `ain7` `ain8` `ain9`     | -                       | PA7 PA6 PA5 PA2 PA1 PA0 | ADC4 inputs                                                      |
| `ain10`                                       | -                       | PB9                     | ADC4 input IN10                                                  |
| `tim1ch1` `tim1ch2` `tim1ch3` `tim1ch4`       | PA8 PA9 PA10 PA11       | PA11 PA12 PB4 PB3       | TIM1 channels 1-4                                                |
| `tim1ch1n` `tim1ch2n` `tim1ch3n`              | PA7 PB8 PB9             | PB2 PB1 PB0             | TIM1 complementary channels 1N-3N                                |
| `tim1bkin`                                    | PB12                    | PA2                     | TIM1 break input                                                 |
| `tim2ch1` `tim2ch2` `tim2ch3` `tim2ch4`       | PA15 PA1 PA2 PA3        | -                       | TIM2 channels 1-4 (32-bit counter)                               |
| `tim2ch1` `tim2ch3` `tim2ch4`                 | -                       | PA5 PA7 PA6             | TIM2 channels 1, 3, 4 (CH2 is the terminal RX pin)               |
| `tim3ch1` `tim3ch2` `tim3ch3` `tim3ch4`       | -                       | PA10 PA1 PB14 PB9       | TIM3 channels 1-4                                                |
| `tim16ch1` `tim17ch1`                         | PA6 PB9                 | PB9 PA1                 | TIM16 / TIM17 channel 1                                          |
| `tim16ch1n`                                   | -                       | PB8                     | TIM16 complementary channel 1N (red LD3 on the pin)              |
| `tim17ch1n`                                   | -                       | PB3                     | TIM17 complementary channel 1N                                   |
| `qei1a` `qei1b`                               | PA8 PA9                 | PA11 PA12               | TIM1 encoder phase A / B                                         |
| `qei1idx`                                     | -                       | PA15                    | index input of the default encoder                               |
| `qei2a` `qei2b` `qei2idx`                     | PA15 PA1 PC6            | -                       | TIM2 encoder phase A / B, index input                            |
| `qei3a` `qei3b`                               | -                       | PA10 PA1                | TIM3 encoder phase A / B                                         |
| `lptim1in1` `lptim1in2`                       | PC0 PC2                 | PA0 PB3                 | LPTIM1 encoder inputs                                            |
| `lptim2in1` `lptim2in2`                       | -                       | PB9 PB0                 | LPTIM2 encoder inputs                                            |
| `lptim1ch2`                                   | -                       | PA15                    | LPTIM1 channel 2 (CH1, PB11, is not bonded out)                  |
| `lptim2ch1` `lptim2ch2`                       | -                       | PA11 PA1                | LPTIM2 channels 1-2                                              |
| `spi1clk` `spi1miso` `spi1mosi` `spi1cs`      | PA5 PA6 PA7 PA4         | PB4 PB3 PA15 PA12       | SPI1 (Arduino D13/D12/D11/D10)                                   |
| `spi1nss`                                     | PA4                     | PA12                    | SPI1 hardware NSS (the pin of `spi1cs`)                          |
| `spi2clk` `spi2miso` `spi2mosi` `spi2nss`     | PB13 PB14 PB15 PB12     | -                       | SPI2                                                             |
| `spi3clk` `spi3miso` `spi3mosi` `spi3nss`     | -                       | PA0 PB9 PB8 PA5         | SPI3 (8- and 16-bit frames only)                                 |
| `i2c1scl` `i2c1sda`                           | PB8 PB9                 | PB2 PB1                 | I2C1 SCL / SDA (Arduino D15/D14)                                 |
| `i2c3scl` `i2c3sda`                           | PC0 PC1                 | PA6 PA7                 | I2C3 SCL / SDA                                                   |
| `qspiclk` `qspincs`                           | PA3 PA2                 | -                       | QUADSPI clock / chip select                                      |
| `qspiio0` `qspiio1` `qspiio2` `qspiio3`       | PB9 PB8 PA7 PA6         | -                       | QUADSPI IO0-IO3                                                  |
| `lpuart1tx` `lpuart1rx`                       | PA2 PA3                 | PB5 PA10                | LPUART1 TX / RX (Arduino D1/D0)                                  |
| `lpuart1rts` `lpuart1cts`                     | PB12 PA6                | PB9 PB15                | LPUART1 RTS / CTS                                                |
| `usart2tx` `usart2rx` `usart2rts` `usart2cts` | -                       | PB0 PA11 PB1 PB2        | USART2                                                           |
| `mco`                                         | PA8                     | -                       | clock output MCO (`clock.mco`)                                   |
| `led0` `led1`                                 | PB0 PB1                 | PB4 PA9                 | user LEDs (green LD2, red LD3 / blue LD1, green LD2 = debug LED) |
| `gpio0`                                       | PC6                     | PB14                    | general-purpose test pin, watchdog warning toggle                |
| `gpio1` `gpio2`                               | PC10 PC12               | PA5 PA0                 | general-purpose test pins                                        |
| `gpio3` `gpio4`                               | PC13 PE4                | -                       | general-purpose test pins                                        |
| `sw1` `sw2` `sw3`                             | PC4 PD0 PD1             | PC13 PB6 PB7            | user buttons (input only, pulled up)                             |

- Wherever a pin is expected, an alias from this table may be used instead of `P<port><index>`; several aliases may name the same pin, and no alias carries a default pull.
- Per-instance default pins apply only when a command gets no pins at all: LPUART1 `lpuart1tx`/`lpuart1rx`; the default encoder (WB55 TIM2 `qei2a`/`qei2b`/`qei2idx`, WBA55 TIM1 `qei1a`/`qei1b`/`qei1idx`). Other UART and encoder instances need their pins; SPI and the ADC always do, PWM needs `channels` or `pins`.
- `board.pins` → `OK <alias>=<pin>,...` lists the table for the running board.
- The buttons short their pin to ground when pressed; never configure `sw1`-`sw3` as outputs.

## Framing

The generic framing (`OK`/`ERR`/`EVT` lines, reasons, the deferred `\r\n` prefix, number, hex and list syntax, open/close semantics) is specified in EMIL's [hardware-in-the-loop terminal documentation](https://github.com/embedded-pro/embedded-infra-lib/blob/main/docs/Hil.md). hal-st adds:

- After reset the firmware prints `EVT boot board=<name> family=<stm32wb55|stm32wba55> sysclk=<hz> reset=<cause>` once; `<cause>` is `iwdg`, `wwdg`, `sw`, `lpwr`, `obl`, `bor`, `pin` or `unknown` (the RCC reset flags in that priority order; a reset through NRST also sets `pin`, so `pin` is checked last).
- Pins are written as `P<port><index>`, for example `PA15`, `PB3`, `PH3`: ports A-E and H on STM32WB55 and A-C and H on STM32WBA55, index 0-15; a pin the package does not bond out returns `ERR pin` (on the STM32WBA55 UFQFPN48 also PA3, PB10, PB11 and PB13, its SMPS and VDD11 pads).
- Instance numbers are the STM32 peripheral numbers: USART 1-2 and LPUART 1 (selected with `lp=1`), I2C 1-3, SPI 1-3, TIM 1-17 (PWM, encoder, timer and timer PWM), LPTIM 1-2 (low-power timer, LPTIM PWM, and the encoder with `lp=1`), QUADSPI 1, ADC 1 (WB55) or 4 (WBA55), watchdog 0 (the WWDG).
  A number the running MCU lacks, including 0 where the peripherals start at 1, returns `ERR range`: both MCUs have I2C1 and I2C3 (not I2C2) and LPTIM1 and LPTIM2; the encoder with `lp=1` takes LPTIM1 on the WB55 and LPTIM1 or LPTIM2 on the WBA55; QUADSPI exists on the WB55 only (the `qspi` commands are unsupported on the WBA55, see below).
- The terminal UART and its pins, the debug LED, the SWD pins PA13/PA14, the LSE crystal pins PC14/PC15 and BOOT0 (PH3) are reserved and cannot be opened (`ERR busy`); any other pin, aliased or not, can be reconfigured freely.
  The debug LED blinks while the firmware runs: WB55 PB5, the blue LD1; WBA55 PA9, the green LD2 (alias `led1`), which is not connected on a stock NUCLEO-WBA55CG (SB28 open), so it stays dark unless SB28 is closed.
  A pin held by another open instance returns `ERR busy`; a pin the hal-st pinout table does not offer for the requested function and instance returns `ERR pin`.
- A timer serves one group at a time: a timer held by an open PWM, encoder, timer-triggered ADC, timer (`tim`) or timer PWM (`tpwm`), by a running `ain.burst` or `dma.wave` (both TIM2), or by the scaffold timer TIM17 while `lpm.enter` or `hsem.lock hold=` uses it, returns `ERR busy` to the other groups.
- One peripheral instance serves one group at a time: an I2C instance is shared by `i2c`, `i2cs` and `eeprom`, an SPI instance by `spi` and `spis`, the ADC by `adc` and `ain`, an LPTIM by `qei` (`lp=1`), `lptim` and `lptpwm`, and the HSEM interrupt (claimed as HSEM 0, the semaphore the RNG driver waits on) by `flash ... variant=coord`, `hsem.lock` and `rng ... variant=hsem` (WB55).
  So are the DMA channels some groups share: WB55 DMA1 channel 7 (`adc`, `ain.burst`); WBA55 GPDMA1 channel 7 (`adc`, `ain.burst`, `spis` receive) and channel 8 (`spis` transmit, `dma.wave`). An instance or channel another group holds returns `ERR busy`.
- RAM limits how many instances are open at the same time: 1 PWM timer, 1 UART besides the terminal, 1 SPI master, 1 SPI slave, 1 I2C master, 1 I2C target, 1 attached EEPROM, 1 ADC, 1 encoder, 1 timer (`tim`), 1 timer PWM (`tpwm`), 1 LPTIM, 1 LPTIM PWM, 1 QUADSPI, 1 watchdog and 8 GPIO pins; one more returns `ERR busy`.
- Argument errors (`usage`, `range`, `pin`, `unsupported`) are reported before `ERR busy`, except for the arguments a command parses into a buffer an operation in flight still uses: the payload of `flash.write`, the payload and `rx` of `spis.arm`, the data arguments of `qspi.cmd` and `qspi.xfer` (see their sections).
- Line length: a command line holds at most 255 characters (`terminal.max_command_length` in the board files). Commands that move more data than fits take a payload the firmware generates and can answer with a CRC instead of the data:
  - in place of a hex payload, `-` with `len=<n>` (1 up to the command's capacity, `ERR range` above) `[pattern=inc|const|prbs]` (default `inc`) `[seed=<0..0xFFFFFFFF>]` (default 0); `len` with a hex payload, or `pattern`/`seed` without `len`, returns `ERR usage`
  - `inc`: byte i is (seed + i) & 0xFF; `const`: every byte is seed & 0xFF; `prbs`: per byte, a 32-bit xorshift `x ^= x << 13; x ^= x >> 17; x ^= x << 5` then `x & 0xFF`, starting from `seed` (1 when `seed` is 0). `inc` with seed 0xFE gives `fe ff 00 01`, `prbs` with seed 1 gives `21 01 c5 4f d1 d0 1a b2`
  - `out=hex|crc` (default `hex`) where a command returns data: `hex` returns at most 128 bytes (`ERR range` above); `crc` returns `len=<n> crc=<8 lower-case hex digits>`, the CRC-32 of zlib (`zlib.crc32`; `inc` seed 0 with 256 bytes gives `29058c73`)
  - durations in replies are `us=<n>` microseconds

## General

- `ping` → `OK`
- `info` → `OK board=<name> family=<family> sysclk=<hz> reset=<cause> uid=<hex>` (the 96-bit unique device ID)
- `reset` → no final line; the board resets and prints `EVT boot ...` with `reset=sw`
- `delay <ms>` → `OK` after the given time (lets the host synchronise with firmware timing)

## Clock (hal-st default clocks, `ConfigureDefaultClockNucleoWB55RG` and `ConfigureDefaultClockNucleoWBA55CG`)

- `clock.info` → `OK sysclk=<hz> hclk=<hz> pclk1=<hz> pclk2=<hz> [pclk7=<hz>] hse=0|1 lse=0|1 hsi=0|1 [hsi48=0|1] pll=0|1 rngsel=<source> [clk48=<source>]`
  - the frequencies come from `HAL_RCC_Get*Freq`, the flags are the oscillator ready flags (`LL_RCC_*_IsReady`; `pll` is PLL1 on STM32WBA55)
  - `pclk7` on STM32WBA55 only; `hsi48` and `clk48` on STM32WB55 only
  - `rngsel` is the RNG kernel clock selection: `clk48`, `lsi` or `lse` on STM32WB55, where `clk48` names the CLK48 source (`hsi48`, `pllsai1`, `pll` or `msi`); `lse`, `lsi`, `hsi` or `pll` (PLL1 Q) on STM32WBA55
- `clock.mco <sysclk|hse|hsi|lse|hsi48|off> [div=1|2|4|8|16]` → `OK` (STM32WB55): MCO source and divider (`LL_RCC_ConfigMCO`, default `div=1`); `off` stops the output. Validation scaffolding: `sgpio.af PA8 af=0` puts MCO on the pin
- `clock.hsi48 <0|1>` → `OK` (STM32WB55): switches HSI48 off or on and waits for its ready flag (`ERR timeout` after 10 ms). Validation scaffolding for the RNG tests; switch it back on afterwards. `clock.hsi48 0` answers `ERR busy` while an `rng` driver holds the RNG clock (a `variant=async` read in flight, or one left by its `ERR timeout` until the next `rng` command): it would lose its kernel clock
- `clock.mco` and `clock.hsi48` return `ERR unsupported` on STM32WBA55: its only MCO pin is the terminal RX (PA8), and it has no HSI48

## GPIO (`hal::GpioPinStm`)

- `gpio.cfg <pin> <in|out|od> [pull=none|up|down] [drive=low|medium|fast|high]` → `OK`; `out` starts low, `od` starts released and takes no pull; `pull` defaults to `none`, `drive` (the `hal::Speed` of the output stage) to `low`
- `gpio.set <pin> <0|1>` → `OK`
- `gpio.get <pin>` → `OK value=<0|1>`
- `gpio.pulse <pin> <count> <periodMs>` → `OK` after `count` toggles of the output, one every `periodMs` (EMIL timer driven); `ERR usage` on an input
- `gpio.irq <pin> <rising|falling|both|off> [type=immediate|dispatched]` → `OK`; each edge increments a counter; `type` defaults to `dispatched`
  - an EXTI line serves one port at a time: a pin whose line (its index) already counts edges for a pin of another port returns `ERR unsupported` until that pin's interrupt is turned `off` or the pin is released
- `gpio.count <pin> [clear=0|1]` → `OK count=<n>`
- `gpio.release <pin>` → `OK`; the pin returns to a digital input with its configured pull

## Synchronous GPIO (`hal::SynchronousOutputPinStm`, `hal::SmallPeripheralPinStm`, `hal::MultiGpioPinStm` with `hal::MultiPeripheralPinStm`)

- `sgpio.out <pin> <0|1> [od=0|1] [speed=low|medium|fast|high]` → `OK`: the first use of a pin builds a `SynchronousOutputPinStm` (push-pull, `speed=low` unless given), later uses set the level; an `od` or `speed` that differs from the pin's rebuilds it, an omitted one keeps its value. The pin has no pull: an open-drain high only releases it
- `sgpio.latch <pin>` → `OK value=<0|1>`: the output latch (`GetOutputLatch`); `ERR notopen` for a pin `sgpio.out` does not hold
- `sgpio.af <pin> timer=<1-17> [ch=<1-4>]` → `OK af=<n>`: a `SmallPeripheralPinStm` (push-pull, low speed, no pull) on the alternate function of channel `ch` (default 1) of TIM`timer` in the hal-st pinout table; a timer the MCU lacks returns `ERR range`, a pin without that channel `ERR pin`
  - `sgpio.af <pin> af=<0-15>` muxes a raw alternate function instead (MCO is AF0 on PA8, alias `mco`); exactly one of `timer` and `af`, and `ch` only with `timer` (`ERR usage`)
  - the group only muxes the pin: what drives the function (a `tpwm` channel with no pin of its own, `clock.mco`) is set up by its own group
- `sgpio.multi <pin>,<pin>[,...] timer=<1-17> [ch=<1-4>]` → `OK`: a `MultiGpioPinStm` over 1-4 different pins (`ERR usage` otherwise) muxed together to channel `ch` (default 1) of TIM`timer` by a `MultiPeripheralPinStm`; a pin without that channel returns `ERR pin`
- `sgpio.release <pin>` → `OK`: the pin returns to an input without pull; any pin of the `multi` set releases the whole set; `ERR notopen` for a pin the group does not hold
- The group holds up to 4 output pins, 4 alternate-function pins and one `multi` set (one more returns `ERR busy`); a pin serves one of them at a time, and a pin another group holds returns `ERR busy`
- `sgpio.out` checks the level, `od` and `speed` before the pin; `sgpio.af` and `sgpio.multi` check `timer`, `ch` and `af` (`usage`, then `range`) before the pins

## PWM (`hal::PwmStm`, `sync=1` selects `hal::SynchronousPwmStm`)

- `pwm.open <timer> [channels=<c>[,<c>...]] [pins=<pin>[:<npin>][,...]] [freq=<hz>] [mode=edge|edgedown|center|centerup|centerboth] [prescaler=<n>] [dead=<ns>|off] [inv=0|1] [invn=0|1] [idle=0|1] [idlen=0|1] [brk=<pin>] [brkpol=low|high] [brkauto=0|1] [sync=0|1] [preload=0|1] [brkfilter=<0-15>] [trgo=reset|enable|update|oc1|oc1ref|oc2ref|oc3ref|oc4ref]` → `OK pwmclk=<hz>`
  - `channels` or `pins` is required (`ERR usage`); 1 to 4 channels, each at most once (`ERR usage`), numbered 1-4
  - a `pins` entry is the channel output, optionally followed by `:` and its complementary output; `-` leaves a position unused (`PA8:PA7` drives CH1 and CH1N, `PA8` or `PA8:-` CH1 only, `-:PA7` CH1N only); with `channels` the entries follow the channel order, without it each channel follows from its pin
  - `channels` without `pins` takes the first pin of the pinout table for each channel that is neither reserved nor missing from the package, and no complementary output
  - defaults: `freq=10000 mode=edge prescaler=0 dead=off inv=0 invn=0 idle=0 idlen=0 brkpol=high brkauto=0 sync=0 preload=1`, no `brkfilter` and no `trgo`
  - `pwmclk` is the counter clock, the timer kernel clock divided by `prescaler + 1`; `prescaler` is 0-65535
  - `mode` selects the counter (`hal::PwmStmBase::Alignment`): `edge` counts up; `edgedown` counts down and cannot reach 0 %: at duty 0 the output stays active for one counter tick per period, since PWM mode 1 is active while the counter is at or below the compare value;
    `center`, `centerup` and `centerboth` count up and down and give the same waveform (they differ only in when the compare flags are set); every mode but `edge` needs a counter mode select, TIM1, TIM2 or (STM32WBA55) TIM3 (`ERR unsupported` on TIM16/TIM17)
  - `preload=1` buffers period and compare values, so a `pwm.duty` or `pwm.freq` takes effect at the next update event; with `preload=0` it takes effect at once, and a write in the middle of a period cuts or stretches that period's pulse
  - `brkfilter` is the break input filter (BDTR.BKF, 0 = none up to 15 = 8 samples at the timer kernel clock / 32, 256 kernel clocks; the reference manual lists every step); it needs `brk` (`ERR usage`); a non-zero filter on a timer without one answers `ERR unsupported` (STM32WB55 TIM16/TIM17, whose BKF reads as zero)
  - `trgo` selects the trigger output (TIMx_CR2.MMS) an `adc.open trgo=` sequence runs on: `reset`, `enable`, `update`, `oc1` (compare pulse of channel 1), `oc1ref`-`oc4ref` (the channel's reference signal); without `trgo` the trigger output is `reset`; it needs a master timer, TIM1, TIM2 or (STM32WBA55) TIM3 (`ERR unsupported` on TIM16/TIM17)
  - `dead` is the dead time inserted between a channel and its complementary output, at most 1000000 ns; it saturates at the largest dead time the DTG field encodes
  - `inv` and `invn` invert every channel output or complementary output, `idle` and `idlen` set their level while the outputs are disabled; the timer never drives a channel and its complementary output to their active levels together, so idle levels that are both active (`idle=1 idlen=1` with `inv=0 invn=0`) leave both at their inactive levels
  - `brk=<pin>` muxes the break input of the timer (`ERR pin` for another pin); `brkpol` is the active level and `brkauto=1` re-enables the outputs automatically after the break input releases
  - complementary outputs, `dead`, `idle`, `idlen` and `brk` need a timer with a break function, TIM1, TIM16 or TIM17 (`ERR unsupported` otherwise)
  - a channel the timer does not have, or a complementary output it does not have (CH4N; CH2-CH4 on TIM16/TIM17), returns `ERR unsupported`
  - each open rebuilds the timer, so no setting of a previous open survives
- `pwm.duty <timer> <duty1%> [duty2%] [duty3%] [duty4%]` → `OK`; one duty per opened channel in channel order, or a single duty for all of them, starts the outputs; duty accepts decimals (`12.5`, up to 4 digits), `0` and `100`
- `pwm.freq <timer> <hz>` → `OK`
- `pwm.stop <timer>` → `OK`
- A frequency whose period is under 2 counter ticks (4 in the centre-aligned modes, which need an auto-reload of at least 2: at 1 the output is a fixed half period), or whose auto-reload does not fit the counter (16 bits, 32 bits on TIM2), returns `ERR range`, both in `pwm.open` and `pwm.freq`;
  with `ticks = pwmclk / hz` (rounded down) the auto-reload is `ticks - 1` for `edge` and `edgedown` and `ticks / 2` for the centre-aligned modes, where the counter runs up and down for a period of `2 x ARR` ticks
- `pwm.close <timer>` → `OK`

## UART (`hal::UartStm`, `dma=1` selects `hal::UartStmDma`, `duplex=1` selects `hal::UartStmDuplexDma`, `sync=1` selects `hal::SynchronousUartStm`, `sendonly=1` selects `hal::SynchronousUartStmSendOnly`)

- `uart.open <index> [lp=0|1] [tx=<pin>] [rx=<pin>] [rts=<pin>] [cts=<pin>] [baud=<bps>] [parity=none|even|odd] [flow=none|rts|cts|rtscts] [swap=0|1] [dma=0|1] [duplex=0|1] [sync=0|1] [sendonly=0|1]` → `OK`
  - `lp=1` selects LPUART`<index>` instead of USART`<index>`; USART1 is the terminal (`ERR busy`)
  - default 115200 8N1 (8 data bits, plus the parity bit when `parity` is not `none`; one stop bit)
  - `baud` is 300-12000000, at most 8000000 on STM32WB55 where the HAL asserts that limit (`ERR range` outside); a rate whose divider does not fit the baud-rate register of the instance at its kernel clock returns `ERR range` (USART: 16 to 65535 with 8× oversampling; LPUART: 0x300 to 0xFFFFF)
  - without pins LPUART1 uses `lpuart1tx`/`lpuart1rx`; every other instance needs `tx` and `rx` (`rx` is optional with `sendonly=1`)
  - `flow` needs the matching `rts`/`cts` pins; `rts` and `cts` alone are only offered by `sync=1`, and `rts` alone by `sendonly=1` (`ERR unsupported` otherwise)
  - `swap=1` exchanges the TX and RX functions of the two pins (`ERR unsupported` with `sync=1` or `sendonly=1`)
  - at most one of `dma`, `duplex`, `sync` and `sendonly` (`ERR usage`)
  - `sync=1` supports only `parity=none` and not `lp=1` (`ERR unsupported`); `duplex=1` does not support `lp=1` (`ERR unsupported`)
  - `sendonly=1` transmits only, polling the data register: `flow` is `none` or `rts` (`cts` and `rtscts` return `ERR unsupported`), `parity=none` only; a given (or default) `rx` pin is held but left unconfigured. With `lp=1` it uses the `SyncLpUart` constructors, which exist on STM32WB55 only: `lp=1 sendonly=1` returns `ERR unsupported` on STM32WBA55
  - with `sendonly=1`, `uart.send` answers once the last byte has left the data register (TXE), while its frame is still on the wire: a `uart.close` right after it may cut that frame; `uart.recv` returns no data
- `uart.send <index> <hex>` → `OK` once the driver reports completion (up to 112 bytes; `ERR timeout` if the driver never completes)
- `uart.recv <index> [timeout=<ms>] [len=<n>]` → `OK data=<hex>` with everything received since the last `uart.recv`, at most 256 bytes (waits up to `timeout`, default 1000, at most 10000, for `len` bytes when given, and returns what arrived even if fewer)
- `uart.close <index>` → `OK`

## SPI master (`hal::SpiMasterStm`, `dma=1` selects `hal::SpiMasterStmDma`, `sync=1` selects `hal::SynchronousSpiMasterStm`, `bits` adds `hal::SpiDataSizeConfiguratorStm`)

- `spi.open <index> clk=<pin> mosi=<pin> miso=<pin> [cs=<pin>] [nss=<pin>] [baud=<hz>] [mode=0|1|2|3] [dma=0|1] [sync=0|1] [bits=<4-16>] [lsb=0|1]` → `OK`; defaults `baud=1000000 mode=0 bits=8 lsb=0`
  - the SPI clock is the fastest `spiclk / 2^n` (n = 1-8) not above `baud`, where `spiclk` is the kernel clock of the instance; `baud` outside `spiclk/256 ... spiclk/2` returns `ERR range`
  - `cs` is a GPIO chip select (any free pin, driven by EMIL's `SpiMasterWithChipSelect` or `SynchronousSpiMasterWithChipSelect`): low during a transfer, released after it unless `continue=1`
  - `dma=1` with `sync=1` returns `ERR usage`
  - `nss` is the hardware slave select of the driver (`slaveSelect`): the pin must offer the instance's NSS function (`ERR pin`) and excludes `cs` (`ERR usage`). The masters configure the NSS pin but initialise the SPI with software NSS, so the pin is never driven (a known gap)
  - `lsb=1` sends and receives the least significant bit first
  - `bits` other than 8 needs `dma=1` (`ERR unsupported`); every transfer then runs with frames of `bits` bits (`SpiDataSizeConfiguratorStm`, installed for the whole open)
  - up to 8 bits each buffer byte is one frame (its low bits); from 9 bits on, each frame takes two buffer bytes, little endian, so `txHex` and `rx` need an even length. Received frames are right aligned, the bits above the frame zero
  - SPI3 of the STM32WBA55 is a limited instance: `bits` 8 or 16 only (`ERR unsupported`)
  - the instance is shared with `spis`: an instance the SPI slave holds returns `ERR busy`
- `spi.xfer <index> <txHex> [rx=<n>] [continue=0|1]` → `OK rx=<hex>`; with an empty `txHex` (`-`) it receives `rx` bytes; `rx` defaults to the length of `txHex`, the transfer lasts max(tx, `rx`) bytes with `txHex` zero-padded, and the first `rx` received bytes are returned (`rx=0` only transmits); at most 64 bytes
- `spi.close <index>` → `OK`

## SPI slave (`hal::SpiSlaveStmDma`)

- `spis.open <index> clk=<pin> miso=<pin> mosi=<pin> nss=<pin>` → `OK`: all four pins are required (`ERR usage`), each must offer its SPI function of the instance (`ERR pin`); the slave runs mode 0, 8-bit frames, MSB first, with the hardware NSS input (low selects it)
  - DMA: STM32WB55 DMA2 channels 1 (transmit) and 2 (receive); STM32WBA55 GPDMA1 channels 8 (transmit) and 7 (receive), shared with `adc`, `ain.burst` and `dma.wave`. The instance held by `spi`, a held channel or pin returns `ERR busy`
- `spis.arm <index> <txHex|-> [rx=<n>] [len=<n>] [pattern=inc|const|prbs] [seed=<n>]` → `OK` once the transfer is handed to the driver (`SendAndReceive`)
  - full duplex: `rx` left out or equal to the transmit length (another value returns `ERR usage`); send only: `rx=0`; receive only: `-` with `rx=<n>`; nothing to send or receive returns `ERR usage`. Lengths 1-1024 (`ERR range`)
  - `-` with `len=<n>` sends a payload generated in firmware (see "Framing"); `pattern`/`seed` without `len`, or `len` with hex, return `ERR usage`
  - a transfer already armed returns `ERR busy`, checked before the payload and `rx` (the armed DMA still reads the transmit buffer)
  - while nothing is armed the slave is disabled: frames a master clocks then are dropped. Frames beyond the armed length are dropped as well; the transfer completes with the armed length
  - receive only sends the DMA's dummy word on MISO (undefined content)
- `spis.result <index> [wait=<ms>] [out=hex|crc]` → `OK done=1 rx=<hex>` (or `OK done=1 len=<n> crc=<hex8>` with `out=crc`) once the transfer is done; `OK done=0` at once when nothing is armed, or after `wait` ms (0-10000, default 1000) with the transfer still armed
  - `out=hex` holds 128 bytes: a longer receive length returns `ERR range`; a send-only transfer answers `rx=` with no data
  - the result stays readable until the next `spis.arm` or `spis.cancel`
  - a second `spis.result` while one waits returns `ERR busy`; `spis.cancel` and `spis.close` answer a waiting `spis.result` with `OK done=0` first
- `spis.cancel <index>` → `OK cancelled=<0|1>`: stops the armed transfer (`CancelTransmission`); `cancelled=1` when it was still running
- `spis.close <index>` → `OK`: cancels the transfer, then releases the driver, its DMA channels and pins

## ADC (`hal::AdcStm` with `hal::AdcDmaMultiChannelStmBase`)

- `adc.open <adc> pins=<pin>[,<pin>...] [sampling=<cycles>] [timer=<timer>] [rate=<hz>] [trgo=<timer>]` → `OK`
  - `pins` is required (`ERR usage`), one conversion per pin in the given order, at most 8 (`ERR range`); a pin without an ADC channel returns `ERR pin`
  - `sampling` is the sampling time of every channel, in ADC clock cycles: WB55 `2.5`, `6.5`, `12.5`, `24.5`, `47.5`, `92.5`, `247.5`, `640.5` (default `2.5`); WBA55 `1.5`, `3.5`, `7.5`, `12.5`, `19.5`, `39.5`, `79.5`, `814.5` (default `3.5`); the value is handed to the driver's per-channel `samplingTime`
  - without `timer` each run converts the sequence once from a software trigger (DMA one-shot mode), and the firmware starts the next run from the event loop after the previous one completed
  - `timer=<t>` triggers the conversions from the TRGO of timer `t` at `rate` runs per second (default 1000, 1-100000, `ERR range` outside) in DMA circular mode; the timers the driver can trigger from are TIM1 and TIM2 (`ERR unsupported` for others); `rate` without `timer` returns `ERR usage`
  - `trgo=<t>` converts the sequence once per trigger output of timer `t` while the `pwm` group drives it (open `pwm.open <t> ... trgo=<source>` first; `adc.open` never takes the timer), in DMA circular mode; nothing arrives before `pwm.duty` starts the timer
    - `trgo` with `timer` or `rate` returns `ERR usage`, a timer the MCU lacks `ERR range`
    - `ERR unsupported` when no group holds the timer, or when the ADC cannot trigger from its TRGO: TIM1 and TIM2 on STM32WB55, TIM2 only on STM32WBA55 (ADC4 reaches TIM1 through TRGO2, which PWM leaves at reset)
    - `ERR busy` when another group (encoder, timer-triggered ADC, `ain.burst`, `dma.wave`, `tim`, `tpwm`) holds the timer
  - while open the group holds the ADC and its DMA channel (channel 7 of DMA1 on STM32WB55, of GPDMA1 on STM32WBA55), which `ain` needs too (`ERR busy` both ways)
- `adc.measure <adc> [n=<samples>]` → `OK samples=<v>[,<v>...]` (raw 12-bit codes); `n` is the number of sequence runs (default 1), each contributing one value per pin, at most 64 values; returns `ERR timeout` after 1000 ms
- `adc.close <adc>` → `OK`

## Analog input (`hal::AnalogToDigitalPinImplStm`, `hal::AnalogToDigitalInternalTemperatureStm`, `hal::AdcTriggeredByTimerWithDma`)

Each command builds its drivers, holds the ADC (and for `ain.burst` TIM2 and the ADC's DMA channel, which `spis` receives on as well on STM32WBA55) until it answers and then releases them; while `adc` is open, another group holds one of them, or a command of this group still runs, it answers `ERR busy`. `<adc>` is the ADC of `adc.open` (1 on STM32WB55, 4 on STM32WBA55, `ERR range` otherwise).

- `ain.read <adc> <pin|temp> [sampling=<cycles>]` → `OK code=<v>`, or `OK code=<v> mcelsius=<n>` for `temp`
  - one conversion of `pin` (an analog pin, `ERR pin` otherwise) or of the internal temperature sensor; `sampling` takes the values of `adc.open` (default the same); the STM32WBA55 driver samples every channel with its common sampling time (79.5 cycles) whatever `sampling` says
  - `mcelsius` is `__LL_ADC_CALC_TEMPERATURE` with VDDA = 3300 mV and the factory calibration, in whole degrees (a multiple of 1000); the sensor needs a long sampling time (WB55 `640.5`, WBA55 `814.5`), the driver's default is shorter than its minimum
  - `ERR timeout` after 1000 ms
- `ain.burst <adc> <pin> n=<1-256> rate=<10-100000> [repeat=1|2] [out=list|stats]` → `OK samples=<v>,... us=<n>` or `OK n=<n> min=<v> max=<v> mean=<v> us=<n>`
  - one `AdcTriggeredByTimerWithDma` over a 256-sample buffer converts `pin` on every TIM2 update at `rate` per second (TIM2 is the driver's timer); each command calls `Measure(n)` `repeat` times on that driver
  - `n` and `rate` are required (`ERR usage`); `out=list` (default) lists the samples and allows `n` up to 64 (`ERR range` above), `out=stats` reports the number of samples, their minimum, maximum and mean (rounded); `us` is the time from `Measure(n)` to its result
  - with `repeat=2` the first measurement is reported as `EVT ain index=<adc> run=1 ...` with the same fields, the second as the final line
  - `ERR timeout` after `repeat` × (n / rate + 1 ms) + 1000 ms; the group stays busy until the measurement completes

## DMA (`hal::CircularTransmitDmaChannel`)

- `dma.wave <pin> rate=<1-1000000> pattern=<hex> [ms=<1-10000>]` → `OK` after `ms` (default 50)
  - drives `pin` (an output) with `pattern`, 1 to 32 bytes, one bit per TIM2 update at `rate` per second, byte by byte and least significant bit first, repeated: TIM2 update requests make a `CircularTransmitDmaChannel` write a 32-bit set/reset word to the port's BSRR (32-bit memory and peripheral transfers)
  - the channel is DMA2 channel 4 on STM32WB55 and GPDMA1 channel 8 on STM32WBA55; TIM2, the channel and the pin are held until the reply, so a group using any of them answers `ERR busy`, and so does `dma.wave` while they are in use
  - `rate` and `pattern` are required (`ERR usage`); `pattern=-` or an odd number of hex digits is `ERR usage`, more than 32 bytes `ERR range`

## Quadrature encoder (`hal::SynchronousQuadratureEncoderStm`, `lp=1` selects `hal::SynchronousQuadratureEncoderLpTimStm`)

- `qei.open <timer> [lp=0|1] [a=<pin>] [b=<pin>] [idx=<pin>] [res=<n>] [offset=<n>] [inva=0|1] [invb=0|1] [cap=a|b|ab|rise|fall] [filter=<0-15>] [vel=<us>|off]` → `OK`
  - defaults `res=4096 offset=0 inva=0 invb=0 cap=ab filter=0 vel=1000`; without pins the default encoder takes its default pins, other instances need `a` and `b`
  - `a` and `b` are channels 1 and 2 of the timer; the timer must offer encoder mode (TIM1, TIM2, TIM3; `ERR unsupported` for TIM16/TIM17)
  - `res` is the count at which the counter wraps (2 to 65536, up to 4294967295 on TIM2), `offset` the starting count (below `res`, `ERR range` otherwise)
  - `cap=ab` counts both edges of both phases, `cap=a` and `cap=b` both edges of one phase; `cap=rise` and `cap=fall` need `lp=1` (`ERR usage` otherwise)
  - `vel` is the speed sampling period in µs (1-1000000), `off` leaves speed at 0
  - `idx` is a plain input read by `qei.index`; it never changes the count
  - `lp=1` selects the LPTIM encoder: LPTIM1 on STM32WB55 (LPTIM2 has no encoder interface, `ERR range`), LPTIM1 and LPTIM2 on STM32WBA55. It takes `a`/`b` on the LPTIM inputs 1/2 (`lptim<n>in1`/`lptim<n>in2`), `res` up to 65536, `inva=1` as the mirrored-mounting reversal and `filter` 0, 2, 4 or 8 (consecutive samples)
  - with `lp=1`, `cap=ab` (default) counts both edges of both inputs, `cap=rise` and `cap=fall` only the rising or falling edges of both inputs (two counts per quadrature cycle); `cap=a`, `cap=b`, `offset` and `invb` return `ERR unsupported`
  - the LPTIM of an `lp=1` encoder is held against `lptim` and `lptpwm` (`ERR busy`)
- `qei.read <timer>` → `OK pos=<n> dir=<fwd|rev> speed=<n> res=<n>` (`speed` is in counts per second, `res` is the driver's `Resolution()`)
- `qei.index <timer>` → `OK idx=<0|1>`, the level of the index input; `ERR unsupported` when the encoder was opened without `idx`
- `qei.close <timer>` → `OK`

## Timer (`hal::FreeRunningTimerStm`, `irq=immediate|dispatched` selects `hal::TimerWithInterruptStm`)

- `tim.open <timer> [prescaler=<0-65535>] [period=<n>] [irq=immediate|dispatched|none] [mode=up|down] [pin=<pin>]` → `OK timclk=<hz>`
  - defaults `prescaler=0 period=999 irq=dispatched mode=up`; `period` is the auto-reload, 1-65535 (1-4294967295 on TIM2); `timclk` is the timer kernel clock
  - one update per `period + 1` ticks of `timclk / (prescaler + 1)`
  - `irq=none` builds a `FreeRunningTimerStm` without interrupt; `immediate` and `dispatched` build a `TimerWithInterruptStm` whose update callback runs in the interrupt or from the event loop. Dispatched callbacks coalesce while one is queued: above about 1 kHz `irqs` counts fewer than the updates
  - with `irq=immediate|dispatched` an update rate `timclk / ((prescaler + 1) (period + 1))` (integer division) above 100 kHz returns `ERR range`: the interrupt runs on every update in both modes and would starve the event loop
  - `pin` (any free bonded pin) is driven low and toggled by every update callback, so it runs at half the update rate; `pin` with `irq=none` returns `ERR usage`
  - `mode=down` counts down from `period`; it needs `irq=none` and a timer with a counter mode select (TIM1, TIM2, TIM3), `ERR unsupported` otherwise
- `tim.start <timer>`, `tim.stop <timer>` → `OK`; starting a running or stopping a stopped timer changes nothing
- `tim.count <timer>` → `OK cnt=<n> irqs=<n>`: the counter register and the update callbacks since `tim.open`
- `tim.close <timer>` → `OK`

## Timer PWM (`hal::TimerPwmWithChannels<N>`)

- `tpwm.open <timer> pins=<pin|->[,<pin|->...] [prescaler=<0-65535>] [period=<n>]` → `OK timclk=<hz>`
  - N is the number of entries (1-4) and entry n is channel n; `-` leaves a channel unused (a `hal::DummyPinStm`: the channel is configured but drives no pin, and the pin stays free); at least one entry is a pin (`ERR usage`)
  - defaults `prescaler=0 period=6399` (10 kHz at 64 MHz); the PWM frequency is `timclk / ((prescaler + 1) (period + 1))`, `period` 1-65535 (1-4294967295 on TIM2)
  - a pin that is not channel n of the timer returns `ERR pin`; a channel the timer does not have (CH2-CH4 on TIM16/TIM17) `ERR unsupported`
- `tpwm.duty <timer> <channel> <0-100>` → `OK`: integer percent, compare value = `period` × duty / 100 (`PwmChannelGpio::SetDuty`, 32-bit arithmetic); the output is high while the counter is below the compare value, so 100 keeps one low counter tick per period
- `tpwm.pulse <timer> <channel> <on> <period>` → `OK`: compare value `on` (0 to the counter maximum) and auto-reload `period` (1 to the counter maximum, `PwmChannelGpio::SetPulse`); the period applies to every channel
- `tpwm.start <timer> [ch=<channel>]`, `tpwm.stop <timer> [ch=<channel>]` → `OK`: one channel, or every channel through `TimerPwmBaseStm::Start`/`Stop`; starting a running channel changes nothing
- `tpwm.close <timer>` → `OK`; every channel stops first

## Low-power timer (`hal::FreeRunningLowPowerTimerStm`, `irq=immediate|dispatched` selects `hal::LowPowerTimerWithInterruptStm`)

- `lptim.open <index> [period=<1-65535>] [prescaler=1|2|4|8|16|32|64|128] [irq=immediate|dispatched|none] [rep=<0-255>] [pin=<pin>]` → `OK lptimclk=<hz>`
  - LPTIM 1-2; defaults `period=999 prescaler=1 irq=dispatched rep=0`; `prescaler` is the clock divider, another number returns `ERR range`
  - one update per `period + 1` ticks of `lptimclk / prescaler`, and with `rep` once every `rep + 1` periods; `lptimclk` is the LPTIM kernel clock (PCLK1 on STM32WB55; PCLK7 for LPTIM1 and PCLK1 for LPTIM2 on STM32WBA55)
  - `rep` is the repetition counter of the STM32WBA LPTIM (`ERR unsupported` on STM32WB55, whose LPTIM has none)
  - `irq` and `pin` as for `tim.open`; with `irq=immediate|dispatched` an update rate `lptimclk / (prescaler (period + 1))` (integer division, `rep` left out: the autoreload-match interrupt runs every period) above 100 kHz returns `ERR range`
- `lptim.start <index>`, `lptim.stop <index>`, `lptim.count <index>` → `OK cnt=<n> irqs=<n>`, `lptim.close <index>` as for `tim`

## LPTIM PWM (`hal::LpTimerPwmWithChannels<N>`, NUCLEO-WBA55CG only)

- `lptpwm.open <index> pins=<pin|->[,<pin|->] [prescaler=1|2|4|8|16|32|64|128] [period=<1-65535>]` → `OK lptimclk=<hz>`
  - N is the number of entries (1-2), `-` as for `tpwm`; defaults `prescaler=1 period=6399`; the PWM frequency is `lptimclk / prescaler / (period + 1)`
  - LPTIM1 channel 1 (PB11) is not bonded out on the UFQFPN48, so LPTIM1 runs as `pins=-,<channel 2 pin>`
- `lptpwm.duty`, `lptpwm.pulse`, `lptpwm.start`, `lptpwm.stop` and `lptpwm.close` as for `tpwm`; the LPTIM takes compare and period writes only while it is enabled, so set them after `lptpwm.start`

## Watchdog (`hal::WatchDogStm`, the window watchdog)

- `wdt.start <index> timeout=<ms> [feed=auto|manual] [pin=<pin>]` → `OK`
  - index 0 is the WWDG; `timeout` 1-30000
  - the firmware picks the smallest WWDG prescaler (1, 2, 4, ..., 128) whose early-warning period, 63 × 4096 × prescaler / PCLK1, is at least `timeout`; a longer `timeout` returns `ERR range` (about 516 ms on WB55 and 330 ms on WBA55)
  - with `feed=auto` (default) the firmware refreshes on every early warning; the watchdog resets the board one counter tick after a warning that is not answered
  - `pin` is driven low and toggled in the early-warning interrupt, so its period can be measured; it stays claimed until reset
  - a started watchdog cannot be stopped: it runs until reset
  - the coordinated flash driver (`flash ... variant=coord`, `flash.stack starting`, STM32WB55) borrows the WWDG to refresh it around the steps it runs with the interrupts masked: a running watchdog keeps running, an unstarted one is created for the borrow
  - while an unstarted watchdog is borrowed, `wdt.start` returns `ERR busy` (after the argument checks) until the driver gives it back
- `wdt.feed <index>` → `OK`
- Early warning: `EVT wdt index=0 warning=<n>`. After a watchdog reset the next `EVT boot` reports `reset=wwdg`.

## I2C master (`hal::I2cStm`)

- `i2c.open <index> scl=<pin> sda=<pin> [freq=<hz>] [timing=<hex>] [pull=none|up]` → `OK timing=<0x........> kernel=<hz>`
  - checks in order: `scl` or `sda` missing, or `freq` together with `timing` → `ERR usage`; an index other than 1 and 3, `freq` outside 20000-400000 → `ERR range`; a pin without the `i2cScl`/`i2cSda` function of that instance → `ERR pin`; the instance held by `i2c`, `i2cs` or `eeprom.attach`, or a pin held → `ERR busy`
  - neither `freq` nor `timing`: the driver's default `Config` (TIMINGR 0x70B03D3D on STM32WB/WBA: Standard mode at 63.8 kHz with a 64 MHz kernel clock, 99.2 kHz at 100 MHz); `freq` takes the TIMINGR below; `timing` is written as given (`HAL_I2C_Init` drops the reserved bits 24-27)
  - `timing` in the reply is TIMINGR read back from the peripheral, `kernel` the I2C kernel clock
  - `pull=up` turns on the MCU pull-ups of both pins after the driver configured them (validation scaffolding; the pinout table configures the I2C pins open drain without pull): enough for Standard mode on a short bus without resistors; Fast mode needs external pull-ups. Default `none`
- `freq` → TIMINGR (`I2cTiming`, validation/firmware/I2cTiming.cpp), in integer picoseconds (`tclk = 10^12 / kernel`, `target = 10^12 / freq`, both rounded down)
  - Standard mode up to 100 kHz, Fast mode above: tLOW > 4700/1300 ns, tHIGH >= 4000/600 ns, rise 640/250 ns, fall 20/100 ns, tSU;DAT 250/100 ns, tVD;DAT 3450/900 ns; analog filter 50-260 ns, digital filter off
  - per prescaler 0-15: the first SCLDEL with `(SCLDEL + 1) × tpresc >= rise + tSU;DAT` and the first SDADEL with `fall - 50 ns - 3 × tclk <= (SDADEL × (PRESC + 1) + 1) × tclk <= tVD;DAT - rise - 260 ns - 4 × tclk` (both bounds at least 0)
  - then the minimal SCLL/SCLH meeting tLOW/tHIGH, with `sync = 50 ns + 2 × tclk` added to each phase, and the fewest `SCLL + SCLH + 2` clock units with `2 × sync + fall + units × tpresc >= target`, the extra units going to SCLL first
  - SCLL and SCLH stay within 255; the prescaler with the shortest such period wins (ties: the smaller prescaler). With a zero rise time the bus never runs faster than `freq`; a slow rise makes it slower
  - examples:

    | kernel  | 20 kHz     | 50 kHz     | 100 kHz    | 400 kHz    |
    |---------|------------|------------|------------|------------|
    | 16 MHz  | 0x20408186 | 0x00e097a2 | 0x00e04752 | 0x00500916 |
    | 32 MHz  | 0x80305659 | 0x2090656c | 0x10e04853 | 0x00b0162e |
    | 64 MHz  | 0x90509ca1 | 0x80604348 | 0x40b03943 | 0x10b0172f |
    | 100 MHz | 0xd060aeb4 | 0x70b0777f | 0x50e04b57 | 0x20b11931 |

- `i2c.write <index> <addr> <hex|-> [next=stop|restart|continue] [len=<n>] [pattern=] [seed=]` → `OK sent=<n> result=<complete|nack|buserror>`
  - `addr` 0-0x7f, 0 being the general call; the data is hex or generated with `len` (1-1024, `pattern=inc|const|prbs` `seed=`, see "Line length" in General); `-` without `len` writes no data (an address probe, NBYTES=0)
  - `next` maps to `hal::Action`: `stop` (default), `restart` (no STOP, the next transfer starts with a repeated START), `continue` (the next transfer continues the same transaction without START or address)
  - `sent` is `numberOfBytesSent`, the acknowledged data bytes; `result` maps `Result::complete`, `partialComplete` (`nack`: the address or a data byte was not acknowledged) and `busError` (`buserror`: bus error or arbitration lost)
  - `ERR timeout` after 1000 ms without completion; the group then answers `ERR busy` until `i2c.close`
- `i2c.read <index> <addr> <len> [next=stop|restart|continue] [out=hex|crc]` → `OK result=complete data=<hex>` or, with `out=crc`, `OK result=complete len=<n> crc=<crc32>`; `OK result=nack` or `OK result=buserror` without data
  - `len` 1-1024, above 128 only with `out=crc`; `next` and the timeout as for `i2c.write`
- `i2c.close <index>` → `OK`; disables the interrupts and destroys the driver one event-loop turn later, so completions already queued run first; a held bus (`next=continue`) is released
- `EVT i2c index=<i> hook=notfound|buserror|arblost` when the driver calls `DeviceNotFound`, `BusError` or `ArbitrationLost`, printed before the final line of the transfer

## I2C target (validation scaffolding, LL on the other instance)

Not a hal-st driver (hal-st has no I2C slave): the other end of the bus for the `i2c` tests, written with the LL I2C functions on the EV/ER interrupts of its instance (`hal::cortex::ImmediateInterruptHandler`). Writes use slave byte control: every received byte stops SCL before its acknowledge, so the target decides ACK or NACK per byte; reads load one byte per transmit request.

- `i2cs.open <index> scl=<pin> sda=<pin> [addr=<0x08-0x77>] [mode=regs|sink] [timing=<hex>]` → `OK addr=<0x..>`
  - checks as for `i2c.open`; the instance must not be held by `i2c` or `eeprom.attach` (`ERR busy`)
  - default `addr=0x42` (0x50-0x57 stay free for an EEPROM)
  - `mode=regs` (default): a 256-byte register file; the first byte of a write sets the pointer, the following bytes are stored at pointer++ (wrapping at 256), reads return the registers from the pointer on; `mode=sink`: written bytes only update the counters and the CRC, reads return the pattern of `i2cs.cfg`, from its start in every read
  - `timing` only sets the data setup and hold times (SCLDEL, SDADEL); default the 400 kHz `I2cTiming` value for the kernel clock
- `i2cs.cfg <index> [nack=<k>] [addrnack=0|1] [stretch=<us>] [stretchat=<k>] [fault=none|stop] [faultat=<k>] [pattern=inc|const|prbs] [seed=<n>]` → `OK`
  - replaces the whole behaviour: options left out take their defaults `nack=0 addrnack=0 stretch=0 stretchat=0 fault=none faultat=1 pattern=inc seed=0`
  - `nack=k` NACKs data byte k (1-based) of every write (0 = never); the NACKed byte is not counted or stored
  - `addrnack=1` turns the own address off: the address is NACKed
  - `stretch` (0-10000 us) holds SCL low at byte `stretchat`: 0 is the address phase (both directions), k >= 1 a written byte before its acknowledge, or a read byte before it is loaded; for read bytes after the first the byte in flight shortens the visible gap. The target busy-waits in its interrupt, which blocks the terminal meanwhile
  - `fault=stop faultat=k` (k >= 1) applies to the next read once: when byte k is requested, the target drives SDA and SCL low as GPIO open-drain outputs, releases SCL, waits up to 1 ms for SCL to read high and releases SDA: a STOP in the middle of a byte, which the master reports as a bus error. The target then resets (PE off and on, own address kept) and restores the pins. Run it at 100 kHz
- `i2cs.status <index> [clear=0|1]` → `OK rx=<n> tx=<n> crc=<crc32> writes=<n> reads=<n> stops=<n> nacked=<n> errors=<n> last=<hex>`
  - since the open or the last `clear=1` (which clears after printing): received and transmitted data bytes, the CRC-32 of the received bytes, addressed writes and reads, STOP conditions, NACKed data bytes, bus errors, arbitration losses and overruns seen by the target, and the first 32 data bytes of the last write
- `i2cs.regs <index> <offset> <hex>` → `OK`; preloads the register file (offset + length at most 256)
- `i2cs.dump <index> <offset> <len>` → `OK data=<hex>`; `len` 1-128, offset + length at most 256
- `i2cs.close <index>` → `OK`

## EEPROM (`services::HilEepromCommands` over the `I2cEepromStm` adapter)

- `eeprom.attach <index> scl=<pin> sda=<pin> [addr=<0x08-0x77>] [size=<bytes>] [page=<bytes>] [abytes=1|2] [freq=<hz>] [wcycle=<ms>]` → `OK`
  - defaults for a 24LC256/AT24C256: `addr=0x50 size=32768 page=64 abytes=2 freq=400000 wcycle=10`
  - `size` 1-65536, `page` 8-256 and a power of two, `abytes` the word address bytes (with `abytes=1` at most 256 bytes), `wcycle` 0-20 ms, `freq` as for `i2c.open` (`ERR range` outside); checks in the order of `i2c.open`
  - the adapter builds its own `hal::I2cStm`, whose hooks only count (no `EVT i2c`); `ERR busy` when attached already or when the instance or a pin is held
- `eeprom.detach` → `OK`; `ERR busy` while an adapter transfer is in flight, `ERR notopen` when nothing is attached; after an error (below) it first completes the pending `eeprom.*` command (which prints nothing if it timed out already) and then detaches, so the group works again
- `eeprom.write <address> <hex>` → `OK`, `eeprom.read <address> <len>` → `OK data=<hex>` (`len` 1-128), `eeprom.erase` → `OK`: EMIL's group (`HilEepromCommands`), `ERR range` beyond `size`, `ERR timeout` after 5 s. Without an attached EEPROM the size is 0: writes and reads answer `ERR range`, `eeprom.erase` `OK` at once
- the adapter:
  - writes page by page: each page is one transfer of the word address and the data, ending with a STOP
  - after each page it polls: it repeats only the word address (which starts no write cycle) from each completion until the chip acknowledges, at most `wcycle` ms; `wcycle=0` does not poll
  - reads send the word address, then read after a repeated START
  - `eeprom.erase` writes 0xFF pages over `size` (about 6.7 ms per 64-byte page at 400 kHz: attach with a `size` that fits EMIL's 5 s)
  - a NACK or bus error outside the polling prints `EVT eeprom error=<nack|buserror> address=<a>` and leaves the command pending (EMIL answers `ERR timeout`); `eeprom.detach` recovers

## Random number generator (`hal::SynchronousRandomDataGeneratorStm`, `variant=async` selects `hal::RandomDataGeneratorStm`, `variant=hsem` selects `hal::SynchronousSynchronizedRandomDataGeneratorStm`)

- `rng.read <len> [variant=sync|async|hsem] [lock5=0|1]` → `OK data=<hex>`: `len` random bytes, 1-128; after a `variant=hsem` read the line ends with `hsi48=<0|1>`, the HSI48 ready flag (`LL_RCC_HSI48_IsReady`) once the driver is done
  - every command builds the driver of its variant and destroys it afterwards; `variant` defaults to `sync`
  - `variant=async` answers from the driver's completion (RNG interrupt), `ERR timeout` after 1 s
  - `variant=hsem` exists on STM32WB55 only (`ERR unsupported` on STM32WBA55): the driver holds HSEM semaphore 0 during the read, through the firmware's one `SynchronousHardwareSemaphoreMasterStm` (shared with `hsem` and `flash`)
  - unless this core holds semaphore 5 (clock configuration), the driver's `Hsi48Enabler` starts HSI48, the RNG kernel clock, for the read when it is off and stops it again afterwards
  - `variant=hsem` answers `ERR busy` while this core holds semaphore 0 (`hsem.take 0`: nothing could free it while the driver blocks the event loop) or the coordinated flash driver exists (HSEM 0 of the sharing rule), and `ERR failed` while this core holds semaphore 5 (`lock5=1` or `hsem.take 5`, any process) and HSI48 is off
  - on STM32WB55 `variant=sync` and `variant=async` answer `ERR failed` while HSI48, their kernel clock (CLK48), is off: only `Hsi48Enabler` starts it, and the plain drivers would abort on the clock error
  - `lock5=1` (validation scaffolding, STM32WB55 only: `ERR unsupported` on STM32WBA55; only with `variant=hsem`: `ERR usage` otherwise) holds semaphore 5 (`HAL_HSEM_FastTake`, process 0) around the read, so `Hsi48Enabler` takes its locked branch; `ERR busy` when semaphore 5 is taken, `ERR failed` when HSI48 is off (the driver asserts that HSI48 runs while semaphore 5 is locked)
- `rng.stats <len> [variant=sync|async|hsem]` → `OK n=<bytes> ones=<n> runs=<n> chisq=<x1000> crc=<hex8> us=<n>`: `len` bytes, 16-65536, generated by one driver in chunks of 256 bytes; `variant=async` answers `ERR timeout` after 5 s; `variant` follows the rules of `rng.read`
  - the bytes form one bit stream, each byte most significant bit first: `ones` counts its set bits, `runs` its bit transitions plus one
  - `chisq` is the chi-square of the byte histogram against the uniform distribution (255 degrees of freedom) times 1000, rounded down and at most 4294967295; `crc` is the CRC-32 of the bytes (8 lower-case hex digits, as `out=crc`); `us` is the generation time
- After an `ERR timeout` of `variant=async` the next `rng` command drops that driver and runs; until the timeout another `rng` command returns `ERR busy`

## AES (`hal::SynchronousAes128EcbStm`)

- `aes.enc <key> <data> [swap=none|half|byte|bit]` → `OK data=<hex>`: AES-128 ECB encryption of `data` under `key`; `aes.dec` takes the same arguments and decrypts
  - `key` is 16 bytes (32 hex digits) and `data` 16-80 bytes in whole 16-byte blocks; any other length, an empty (`-`) or malformed hex returns `ERR usage`
  - `swap` is the driver's data swapping (`DataSwapping`, AES_CR.DATATYPE) of every 32-bit word written to and read from the peripheral, default `byte`, which gives standard AES on the byte string; the key is not swapped
  - `none`, `half` and `bit` give `T(AES(T(data)))`, where T reverses the bytes of every 32-bit word (`none`), swaps the two bytes of every 16-bit half-word (`half`) or reverses the bits of every byte (`bit`)
  - every command builds the driver, sets the key, runs and destroys the driver

## Public key accelerator (`hal::PkaStm` on `services::secp256r1`)

- `pka.mul [k=<hex>] [x=<hex> y=<hex>]` → `OK x=<hex> y=<hex> us=<n>`: the scalar multiplication k × (x, y) on the NIST P-256 curve; each result coordinate has 32 bytes (64 hex digits); `us` runs from the start of the operation to its completion
  - `k` defaults to 1 and the point to the base point G of the curve; `x` and `y` come together (`ERR usage` otherwise)
- `pka.check x=<hex> y=<hex>` → `OK on=<0|1>`: whether (x, y) lies on the curve
- `pka.cmp a=<hex> b=<hex>` → `OK cmp=lt|eq|gt`: compares two numbers of the same length, 4-60 bytes: different lengths return `ERR usage`, a length outside 4-60 `ERR range` (60 bytes keep the line within 255 characters)
- Operands are big-endian hex. `mul` and `check` take 1-32 bytes and the firmware left-pads them with zeros to 32 bytes, as `PkaStm` itself pads by two words only. A missing operand, an empty (`-`), odd or non-hex one returns `ERR usage` and one above the size limit `ERR range`; every `usage` check of a command comes before its `range` checks
- The answer comes from the driver's completion (PKA interrupt, dispatched to the event loop), `ERR timeout` after 5 s. One operation runs at a time: until its timeout another `pka` command returns `ERR busy`; after an `ERR timeout` the next one starts a new operation
- The firmware builds one `PkaStm` on the first `pka` command and keeps it: its constructor waits for the PKA to initialise, on STM32WBA55 from the RNG kernel clock

## Internal flash (`hal::FlashHomogeneousInternalStm`, `hal::FlashInternalStm`, their `hal::Synchronous*` twins, on STM32WB55 `hal::FlashCoordinatedWithWirelessStack`)

- The commands work on a scratch region of the flash; addresses are relative to the region:
  - STM32WB55: absolute pages 64-143, 0x08040000-0x0808FFFF in 4 KB pages, past the image and below the secure area of the wireless stack (the region ends at the secure flash start address when that is lower)
  - STM32WBA55: absolute pages 64-127, 0x08080000-0x080FFFFF in 8 KB pages
- `variant=sync|async|coord` (default `sync`) selects the driver; each command builds its driver and destroys it when done:
  - `sync`: the `Synchronous*InternalStm` classes
  - `async`: the `hal::Flash` classes (completion scheduled on the event loop)
  - `coord` (STM32WB55; `ERR unsupported` on STM32WBA55): `FlashCoordinatedWithWirelessStack` over the async driver
- `layout=homogeneous|table` (default `homogeneous`) selects the sectors:
  - `homogeneous`: one sector per page (`Flash*HomogeneousInternalStm`)
  - `table`: a sector-size table (`FlashInternalStm`/`SynchronousFlashInternalStm`) of single pages followed by the page pattern 1, 1, 2, 4 twice; over the 80 pages of the STM32WB55 that is 72 sectors, of which sectors 64-71 have 1, 1, 2, 4, 1, 1, 2, 4 pages; over the 64 pages of the STM32WBA55, 56 sectors with the pattern on sectors 48-55.
    A sector is accepted only from index `first` (the image end page, below) on, so the STM32WB55 region has 80 pages: its multi-page sectors stay accepted for any image below the region (about 60 of its 4 KB pages today)
- `flash.info [variant=] [layout=]` → `OK base=<0x........> sectors=<n> size=<bytes> first=<sector> image=<page> layout=<homogeneous|table>`
  - `image` is the first absolute page past the running image (`_sidata` plus the size of `.data`)
  - `first` is the first sector `flash.erase` and `flash.write` accept, `min(image, sectors)`. Every sector starts at or past the page of its index, so even an erase that took the sector index for the absolute page (as `EraseSectors` did on STM32WB/WBA before its fix) cannot reach the running image
  - when the image reaches page 64, every `flash.*` command returns `ERR unsupported`
- `flash.erase <first> <end> [variant=] [layout=]` → `OK us=<n>`: erases sectors `first` up to `end` (exclusive)
  - `first` below the accepted first sector, `first >= end` or `end` past the last sector return `ERR range`
  - `us` is the duration (CYCCNT); with `variant=sync` it covers the blocking erase
- `flash.write <address> <hex|-> [len=<n>] [pattern=inc|const|prbs] [seed=<n>] [variant=] [layout=]` → `OK us=<n>`
  - the payload is the hex bytes or, with `-` and `len=1..512`, the firmware-generated pattern of the payload convention (`inc` default, `seed` default 0); an empty payload, `len` with hex, or `pattern`/`seed` without `len` return `ERR usage`
  - a payload outside the region or starting before the first accepted sector returns `ERR range`
  - a write that touches a flash word (64 bits on STM32WB55, 128 bits on STM32WBA55) that is not erased returns `ERR failed`: the drivers program whole words (padding with 0xFF), and programming a word twice between erases fails their assertion
- `flash.read <address> <len> [out=hex|crc] [variant=] [layout=]` → `OK data=<hex>` or `OK len=<n> crc=<8 hex digits>`
  - anywhere in the region, `len` from 1 up to the region size; `out=hex` (default) up to 128 bytes, else `ERR range`
- `flash.stack <stopped|starting|fus> [layout=]` → `OK` (STM32WB55)
  - `starting` builds a coordinated driver of `layout` that stays until `stopped` or `fus`, and calls `WirelessStackStarting()`: its steps wait; `starting` while one is held returns `ERR busy`
  - `stopped` and `fus` both call `FirmwareUpgradeServicesReady()`, the driver's only way out of `starting`, which runs a waiting step; the driver is destroyed once idle. Without a held driver they answer `OK`
- Coordinated steps:
  - each step takes HSEM 7 (CPU2's flash semaphore; the tests hold it with `hsem.take 7 procid=<p>`) with the interrupts masked and the watchdog refreshed
  - while HSEM 7 is held or the stack is starting, a step waits for the HSEM interrupt
- Timeouts: `flash.erase` answers `ERR timeout` after 10 s, `flash.write` and `flash.read` after 2 s; an operation that completes after its `ERR timeout` prints `EVT flash op=<erase|write|read> us=<n>`
- Busy rules:
  - while an operation is in flight every flash command returns `ERR busy`, except `flash.read` with `variant=sync` (a plain read), `flash.info` and `flash.stack stopped|fus`; a `flash.write` checks this before its payload
  - an operation stays in flight until it completes, also after its `ERR timeout`: a coordinated step waiting for HSEM 7 runs once the semaphore is released (`hsem.release 7 procid=<p>`), one held by `flash.stack starting` after `stopped` or `fus`
  - while `flash.stack starting` holds the driver, only `variant=coord` commands of its layout run (and the exceptions above)
  - the coordinated driver and `hsem.lock` exclude each other (`ERR busy`): both rely on the HSEM interrupt
  - so does `rng ... variant=hsem`, whose wait on semaphore 0 enables its HSEM interrupt as well: it answers `ERR busy` while the coordinated driver exists

## Hardware semaphore (STM32WB55: `HAL_HSEM_*`, `hal::SynchronousHardwareSemaphoreMasterStm`, `hal::SynchronousHardwareSemaphoreStm`)

- Semaphores 0-31, processes 1-255. The lock state is read from the R register, never from RLR, whose read is itself a one-step lock attempt
- `hsem.take <n> procid=<1..255> [hold=<1..60000 ms>]` → `OK`: a two-step lock (`HAL_HSEM_Take`); `ERR busy` when another process or core holds it
  - with `hold` an EMIL timer releases it after `hold` ms; one `hold` at a time (`ERR busy`)
- `hsem.release <n> procid=<1..255>` → `OK` (`HAL_HSEM_Release`; a release by a process that does not hold it changes nothing); it cancels a pending `hold` of that semaphore and process
- `hsem.status <n>` → `OK locked=0|1 core=<id> procid=<p>`: `core` is the COREID field (4 = CPU1, 8 = CPU2, 0 when free)
- `hsem.lock <n> [hold=<2..65536 us>]` → `OK waited=<us>`: constructs and destroys a `SynchronousHardwareSemaphoreStm` on `n` and reports how long the constructor waited (CYCCNT)
  - with `hold` the firmware first takes `n` for process 1 and starts the scaffold timer (TIM17, 1 us ticks), whose interrupt releases it after `hold` us: the lock must wait that long
  - a semaphore already held returns `ERR busy` (nothing could free it while the lock blocks the event loop); so do TIM17 held by another group (with `hold`) and the coordinated flash driver
- `hsem.mine <n>` → `OK mine=0|1` (`IsLockedByCurrentCore`, a query without side effect)

## Backup RAM (`hal::BackupRamStm` as `hal::BackupRam<volatile uint32_t>`)

- STM32WB55: RTC BKP0R-BKP19R (20 words); STM32WBA55: TAMP BKP0R-BKP15R (16 words). The words survive a system reset (`reset`, the watchdog), not a power cycle
- `bkp.info` → `OK words=<n>`
- `bkp.write <index> <value>` → `OK`: `value` a 32-bit number (decimal or `0x` hex); an index past the last word returns `ERR range`
- `bkp.read <index>` → `OK value=<8 hex digits>`
- `bkp.fill <seed>` → `OK`: word i becomes `seed XOR (0x9E3779B9 × (i + 1))` (32 bits)
- `bkp.check <seed>` → `OK mismatches=<n>`: the words that differ from that fill

## Low power (`hal::LowPowerModeStm`)

- `lpm.enter <sleep|deep> [wake=<pin>] [edge=rising|falling] [marker=<pin>] [timeout=<1..10000 ms>]` → `OK woke=exti restored=<n> us=<n> sleeps=<n>`
  - defaults: `wake` STM32WB55 PC6 (`gpio0`), STM32WBA55 PB14 (`gpio0`); `marker` STM32WB55 PB0 (`led0`), STM32WBA55 PA2 (`tim1bkin`); `edge=rising`; `timeout=2000`
  - the wake pin is an input with the pull against its edge (pull-down for `rising`) and an EXTI interrupt; the marker is an output, high, driven low while the core sleeps and high again right after it wakes
  - inside the window the interrupts are masked (PRIMASK), every NVIC interrupt but the wake line's EXTI and TIM17's is disabled and the SysTick tick interrupt is off: WFI returns only on the wake edge or a TIM17 update
  - the scaffold timer TIM17 counts 1 us ticks (CYCCNT stops while the core sleeps) and gives `us`, the time asleep; at `timeout` ms it ends the window with `ERR timeout`
  - `sleeps` counts the `LowPowerModeStm::Enter` calls: WFI returns only on a TIM17 update (every millisecond) or the wake edge, so a core that sleeps makes about one call per elapsed millisecond plus one, a driver that returns without sleeping hundreds per millisecond
  - `deep` maps to Sleep on STM32WB/WBA (`LowPowerModeStm::Stop`), so it never calls the clock-restore callback: `restored` counts its calls and is 0
  - the wake and marker pins must differ (`ERR usage`); a pin the package lacks returns `ERR pin`, a wake pin whose EXTI line serves a pin of another port `ERR unsupported`, a held pin or TIM17 held by another group `ERR busy`
  - the terminal and every other interrupt (including the watchdog's early warning: a running watchdog is not fed) are blocked for the window: send nothing until the final line

## QUADSPI (`hal::QuadSpiStm`, `variant=dma` selects `hal::QuadSpiStmDma`, `variant=spi` adds `hal::SingleSpeedQuadSpiStmDma`, NUCLEO-WB55RG only)

- `qspi.open <1> [variant=poll|dma|spi] [prescaler=<0-255>] [size=<1-32>]` → `OK clk=<hz>`
  - QUADSPI 1 is the only instance (`ERR range` otherwise); the pins come from the board profile: `qspiclk` PA3, `qspincs` PA2, `qspiio0`-`qspiio3` PB9, PB8, PA7, PA6 (all six held, `ERR busy` when one is taken)
  - defaults `variant=poll prescaler=2 size=24`; `clk` is the QUADSPI kernel clock (HCLK4) divided by `prescaler + 1`; `size` is the flash size as log2 of bytes (DCR FSIZE + 1)
  - `variant=poll` is the blocking `QuadSpiStm` (HAL); `variant=dma` is `QuadSpiStmDma` on DMA2 channel 3; `variant=spi` is `QuadSpiStmDma` with `SingleSpeedQuadSpiStmDma` on top, for `qspi.xfer`
- `qspi.cmd <1> [instr=<0-0xff>] [addr=<n> [abytes=1-4]] [alt=<n> [altbytes=1-4]] [dummy=<0-31>] [lines=1|4] [tx=<hex>|len=<n> [pattern=] [seed=]|rx=<n> [out=hex|crc]] [repeat=<1-8>]` → writes `OK flevel=<n>`, reads `OK data=<hex>` (or `OK len=<n> crc=<crc32>` with `out=crc`)
  - one command of `hal::QuadSpi` (`SendData` without `rx`, `ReceiveData` with it) on every variant; every phase is optional: without `instr`, `addr` and `alt` the command has no instruction, address or alternate-byte phase, and without `tx`/`len`/`rx` no data phase
  - `addr` takes `abytes` bytes (default 3) and `alt` `altbytes` bytes (default 1), both sent most significant byte first; a value wider than its bytes returns `ERR range`, `abytes` without `addr` or `altbytes` without `alt` `ERR usage`
  - `lines` (default 1) applies to every phase; 2 returns `ERR range`
  - `tx` is the data in hex; `len` generates it (`pattern=inc|const|prbs` `seed=`, see "Line length" in General), 1-256 bytes; `rx` reads 1-256 bytes, above 128 only with `out=crc`; at most one of `tx`, `len` and `rx`
  - `flevel` is the QUADSPI FIFO level read in the completion callback: 0 once the last byte has left the pins
  - `repeat` issues the write again from each completion callback (writes only) and answers after the last one
  - a read with a data phase only hangs the QUADSPI with BUSY set (STM32 QUADSPI erratum "cannot be used in indirect read mode when only data phase is activated"): `variant=dma` answers `ERR timeout`; `variant=poll` blocks the firmware for the driver's 5 s HAL timeout, aborts the command and answers `ERR timeout` (the driver never completes a failed command);
    add `dummy=2` to such a read, the erratum's workaround, which drives no IO line either
- `qspi.poll <1> match=<n> mask=<n> [size=1-4] [instr=] [addr=] [abytes=] [alt=] [altbytes=] [dummy=] [lines=1|4]` → `OK`
  - `hal::QuadSpi::PollStatus`: reads `size` status bytes (default 1) until the status masked with `mask` equals `match`
  - `variant=dma`: `ERR timeout` after 2000 ms; the group then answers `ERR busy` until `qspi.close`, which stops the polling (`~QuadSpiStmDma` clears CR)
  - `variant=poll` blocks the firmware until the status matches (`QuadSpiStm` waits with `HAL_MAX_DELAY`, a known gap)
- `qspi.xfer <1> <txhex|-> [len=<n> [pattern=] [seed=]] [rx=<1-128>] [repeat=<1-8>]` → writes `OK flevel=<n>`, reads `OK rx=<hex>`
  - `variant=spi` only (`ERR unsupported` otherwise): `SingleSpeedQuadSpiStmDma::SendAndReceive`, SPI mode 0 with MOSI on IO0, MISO on IO1 and NCS as chip select
  - half duplex: exactly one of the data (`txhex` or `len`) and `rx`, else `ERR usage`; `repeat` as for `qspi.cmd`
  - a read has a data phase only, so the erratum above applies to it
- `qspi.close <1>` → `OK`: also resets the QUADSPI (RCC), so one left BUSY by a timed-out command (the erratum above) is idle at the next `qspi.open`
- `qspi.cmd`, `qspi.poll` and `qspi.xfer` answer within 2000 ms or `ERR timeout`; a command that timed out keeps the group busy until `qspi.close`
- while a command is in flight, `qspi.cmd` returns `ERR busy` before checking its data arguments (`tx`, `len`, `pattern`, `seed`, `rx`, `out`, `repeat`) and `qspi.xfer` before its payload, `rx` and `repeat` (the command in flight uses the data buffer they fill); `qspi.xfer` reports `ERR unsupported` before that

## Not available on these boards

hal-st has no comparator, CAN or Ethernet driver for STM32WB55/STM32WBA55, so `comp.open`, `comp.read`, `comp.irq`, `comp.count`, `comp.close`, `can.open`, `can.send`, `can.close`, `eth.open`, `eth.status` and `eth.close` return `ERR unsupported`.
The groups one MCU lacks return `ERR unsupported` on that MCU:

- STM32WB55: `lptpwm.open`, `lptpwm.duty`, `lptpwm.pulse`, `lptpwm.start`, `lptpwm.stop`, `lptpwm.close` (`hal::LpTimerPwmStm` is not built for STM32WB).
- STM32WBA55: `hsem.take`, `hsem.release`, `hsem.status`, `hsem.lock`, `hsem.mine` (no hardware semaphore), `qspi.open`, `qspi.cmd`, `qspi.poll`, `qspi.xfer`, `qspi.close` (no QUADSPI), `flash.stack` (no wireless coprocessor to coordinate the flash with), `clock.mco` (the only MCO pin is the terminal RX) and `clock.hsi48` (no HSI48).

The `eeprom` commands are served by an external 24Cxx EEPROM on the I2C bus (see the EEPROM section).
