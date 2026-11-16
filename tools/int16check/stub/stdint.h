/* stdint.h for int16check: the same sizes on MSP430 (16-bit int, used to
 * find the arithmetic) and on wasm32 (used to run it). */
#ifndef BB_INT16CHECK_STDINT_H
#define BB_INT16CHECK_STDINT_H
typedef signed char    int8_t;
typedef unsigned char  uint8_t;
typedef short          int16_t;
typedef unsigned short uint16_t;
typedef long           int32_t;
typedef unsigned long  uint32_t;
#endif
