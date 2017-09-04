#ifndef _MATH256_H
#define _MATH256_H

#include <inttypes.h>

typedef struct
{
    uint128_t hi;
    uint128_t lo;
} uint256_t;

const uint256_t ZERO_256_t = {0, 0};

uint_fast32_t are_equal(const uint256_t first, const uint256_t second);
uint_fast32_t is_zero(const uint256_t number);
uint_fast32_t is_smaller(const uint256_t first, const uint256_t second);
uint_fast32_t is_smaller_or_equal(const uint256_t first, const uint256_t second);
uint256_t add(const uint256_t first, const uint256_t second);
uint_fast32_t mod2(const uint256_t number);
uint256_t div2(const uint256_t number);
uint256_t mul2(const uint256_t number);
uint256_t mul3p1(const uint256_t number);
uint_fast32_t mod10(const uint256_t number);
uint256_t div10(const uint256_t number);
uint_fast32_t bitnum(const uint256_t myvalue);
void uint256_t_to_string(uint256_t number, char* string, uint_fast32_t* length);
void printf_256(const uint256_t number);
void fprintf_256( uint256_t number, uint_fast32_t mindigits);

#endif // _MATH256_H
