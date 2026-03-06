# Low-Level C Patterns for Embedded Systems

Q> How would you write directly to a specific memory address in C?

A> The classic embedded systems one-liner:

```c
*(int *)0x12345678 = 0xDEADBEEF;
```

This writes the value `0xDEADBEEF` to memory address `0x12345678` by:

1. Casting the address `0x12345678` to an `int *` pointer
2. Dereferencing it with `*` to access that memory location
3. Assigning the value

This pattern appears constantly in embedded systems for accessing memory-mapped
hardware registers — where specific memory addresses correspond to peripheral
control registers rather than RAM.

## Production-Quality Memory-Mapped I/O

While the one-liner works, production embedded code uses better style for
maintainability and clarity.

### Basic Pattern with Defines

```c
// Memory-mapped hardware register example
#define GPIO_BASE_ADDR  0x40020000
#define GPIO_MODE_REG   ((volatile uint32_t *)(GPIO_BASE_ADDR + 0x00))
#define GPIO_OUTPUT_REG ((volatile uint32_t *)(GPIO_BASE_ADDR + 0x04))

*GPIO_MODE_REG   = 0x00000001;  // Configure GPIO as output
*GPIO_OUTPUT_REG = 0x00000001;  // Set pin high
```

### Struct-Based Approach (Best Practice)

The cleanest approach maps a struct onto the hardware register block:

```c
typedef struct {
    volatile uint32_t MODE;      // Offset 0x00
    volatile uint32_t OUTPUT;    // Offset 0x04
    volatile uint32_t INPUT;     // Offset 0x08
    volatile uint32_t CONFIG;    // Offset 0x0C
} GPIO_TypeDef;

#define GPIO ((GPIO_TypeDef *)0x40020000)

// Now access is clean and type-safe
GPIO->MODE   = 0x00000001;
GPIO->OUTPUT = 0x00000001;
```

This is exactly how ARM's CMSIS (Cortex Microcontroller Software Interface
Standard) handles peripheral access across thousands of microcontrollers.

## The `volatile` Keyword

The `volatile` keyword is **critical** for memory-mapped I/O:

```c
volatile uint32_t *reg = (volatile uint32_t *)0x40020000;
```

Without `volatile`, the compiler might:

- **Optimize away repeated reads** — assuming the value won't change between
  accesses, even though hardware can update it
- **Reorder accesses** — rearrange reads/writes for performance, breaking
  hardware sequencing requirements
- **Cache the value** — store it in a register instead of reading from memory
  each time

Example where this matters:

```c
// Wait for hardware ready flag
while (*STATUS_REG & BUSY_FLAG) {
    // Without volatile, compiler might optimize this to infinite loop
    // It doesn't know hardware will clear BUSY_FLAG
}
```

With `volatile`, the compiler is forced to:

1. Read from memory every time
2. Never cache the value
3. Preserve the exact order of accesses

## Real-World Example: UART Transmission

Here's how you'd actually use this pattern to send data via UART:

```c
typedef struct {
    volatile uint32_t CR;        // Control register
    volatile uint32_t SR;        // Status register
    volatile uint32_t DR;        // Data register
} UART_TypeDef;

#define UART1 ((UART_TypeDef *)0x40013800)

#define UART_SR_TXE  (1 << 7)    // Transmit buffer empty flag
#define UART_CR_UE   (1 << 13)   // UART enable

void uart_send_byte(uint8_t data) {
    // Wait until transmit buffer is empty
    while (!(UART1->SR & UART_SR_TXE)) {
        // volatile ensures we keep checking
    }
    
    // Write data to transmit register
    UART1->DR = data;
}

void uart_init(void) {
    UART1->CR = UART_CR_UE;      // Enable UART
}
```

## Connection to CCSDS Work

This same pattern appears when interfacing with SDR hardware for satellite
communications. CesiumAstro's phased array payloads would use memory-mapped
registers to:

- **Configure RF parameters** — frequency, gain, beam direction
- **Read/write sample buffers** — IQ data streams before CCSDS framing
- **Control DMA transfers** — moving CCSDS packets to/from the SDR
- **Monitor status flags** — PLL lock, temperature, power levels

The CCSDS packet processing in [ccsds_crc.c](ccsds_crc.c) happens in software,
but getting those packets to/from the physical layer requires exactly this kind
of low-level memory-mapped I/O.

## Relevance to Rust

This is one area where Rust's `unsafe` blocks are unavoidable. Even in Rust,
you need raw pointer dereferencing for hardware access:

```rust
const GPIO_BASE: usize = 0x4002_0000;

unsafe {
    let gpio = GPIO_BASE as *mut u32;
    gpio.write_volatile(0x0000_0001);
}
```

The Rust embedded ecosystem (like the `embedded-hal` crate) wraps this in safe
abstractions, but at the bottom layer, it's still casting addresses to
pointers — just like C. See [asRust.md](asRust.md) for more on Rust's approach
to memory safety in embedded contexts.

## Summary

| Pattern                        | Use Case                                    |
| ---                            | ---                                         |
| Raw pointer cast               | Quick one-offs, debugging                   |
| `#define` with `volatile`      | Simple peripherals, legacy code             |
| Struct-based register mapping  | Production code, complex peripherals        |
| `volatile` keyword             | **Always** for hardware registers           |
| CMSIS-style typedefs           | Industry standard for ARM microcontrollers  |

The `*(int *)0x12345678 = 0xDEADBEEF` one-liner is a great interview question,
but real embedded systems engineers use the struct-based approach for
maintainability and type safety.
