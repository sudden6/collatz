#include "math256.h"

uint_fast32_t are_equal(const uint256_t first, const uint256_t second)
{
    return ((first.hi == second.hi) && (first.lo == second.lo));
}

uint_fast32_t is_zero(const uint256_t number)
{
    return are_equal(number, ZERO_256_t);
}

uint_fast32_t is_smaller(const uint256_t first, const uint256_t second)
{
    return ((first.hi < second.hi) || ((first.hi == second.hi) && (first.lo < second.lo)));
}

uint_fast32_t is_smaller_or_equal(const uint256_t first, const uint256_t second)
{
    return ((first.hi < second.hi) || ((first.hi == second.hi) && (first.lo <= second.lo)));
}

uint256_t add(const uint256_t first, const uint256_t second)
{
    uint256_t sum;
    sum.lo = first.lo + second.lo;
    unsigned int carry = sum.lo < first.lo;
    sum.hi = first.hi + second.hi + carry;
    return sum;
}

uint_fast32_t mod2(const uint256_t number)
{
    return number.lo & 1;
}

uint256_t div2(const uint256_t number)
{
    uint256_t result;
    uint128_t carry = number.hi & 1;
    result.hi = number.hi >> 1;
    result.lo = (number.lo >> 1) + (carry << 127);
    return result;
}

uint256_t mul2(const uint256_t number)
{
    uint256_t result;
    uint128_t carry = number.lo >> 127;
    result.hi = (number.hi << 1) + carry;
    result.lo =  number.lo << 1;
    return result;
}

uint256_t mul3p1(const uint256_t number)
{
    uint256_t intermediate = mul2(number);
    intermediate.lo++;

    return add(number, intermediate);
}

const uint_fast32_t pow2_128mod10 = 6;

uint_fast32_t mod10(const uint256_t number)
{
    uint_fast32_t hi = number.hi % 10;
    uint_fast32_t lo = number.lo % 10;

    return ((hi * pow2_128mod10 + lo) % 10);
}

const uint128_t pow2_128div10 = (((uint128_t) 0) - 1) / 10;

uint256_t div10(const uint256_t number)
{
    uint256_t result;
    result.hi = number.hi / 10;
    uint_fast32_t hi_res = number.hi % 10;
    result.lo = number.lo / 10;
    uint_fast32_t lo_res = number.lo % 10;

    result.lo += hi_res * pow2_128div10 + (hi_res * pow2_128mod10 + lo_res) / 10;

    return result;
}

//Berechnet Anzahl Binärstellen; nach gonz
uint_fast32_t bitnum(const uint256_t myvalue)
{
    uint_fast32_t result = 1;
    uint256_t comp = {0, 2};
    while (is_smaller_or_equal(comp, myvalue))
    {
        result++;
        comp = mul2(comp);
    }
    return result;
}
