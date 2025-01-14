#ifndef __PARSE_H
#define __PARSE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <x86gprintrin.h>

static inline float parse_float(char *restrict str, char **restrict end);

#endif // __PARSE_H

#ifdef PARSE_IMPLEMENTATION
/*
  Parse a float with one integer digit, possibly with scientific notation.
  The last bit in the mantissa might differ from when using `strtof` because
  different roundings are used (`parse_float` truncates, `strtof` doesn't).
  The parser requires support for 64 bit integers and the `lzcnt` instruction.
  NOTE: Can only parse floats in the range (-10,10)
*/
static inline float parse_float(char *restrict str, char **restrict end){
  bool sign = false;
  char buf[32] = {0};
  if (*str == '-'){
    sign = true;
    str++;
  }
  // Copy first digit
  buf[0] = *str;
  str++;
  // Skip decimal point
  str++;

  int ndecimals = 0;
  // Copy digits and count number of decimals
  // 17 digits is what is needed to exactly parse a float? Fits in 64 bits.
  for (int i=1; i<18; i++){
    if (*str>'9' || *str<'0'){
      break;
    }
    buf[i] = *str;
    ndecimals++;
    str++;
  }
  // Skip redundant decimals
  while (1){
    if (*str>'9' || *str<'0'){
      break;
    }
    str++;
  }
  // Parse exponent if scientific notation is used
  int sci_exp = 0;
  if (*str == 'e'){
    str++;
    sci_exp = atoi(str);
    // Advance pointer past exponent
    // Skip 1 digit or the sign
    str++;
    while (*str<='9' && *str>='0'){
      str++;
    }
  }
  if (end != NULL){
    *end = str;
  }

  // Count leading zeros in buf. Reinterpret as scientific notation.
  int leading_zs = 0;
  for (size_t i=0; i<sizeof(buf); i++){
    if (buf[i] != '0')
      break;
    leading_zs++;
  }
  sci_exp -= leading_zs;
  ndecimals -= leading_zs;

  uint64_t num = atoll(buf);
  if (num == 0){
    return 0;
  }
  int8_t exp = 0;
  uint32_t mantissa = 0;

  int LZCNT = 0;
  uint64_t tmp_mantissa = 0;
  int exp_adjustment = 0;
  // Adjust for removing the decimal point, leading zeros and scientific notation
  LZCNT = _lzcnt_u64(num);
  // Place highest bit of num as most significant bit
  tmp_mantissa = num << LZCNT;
  for (int i=0; i<ndecimals; i++){
    // Divide by 5 and later adjust for the factor 2's in exponent
    tmp_mantissa /= 5;
    if (i%16 == 0){
      // Shift mantissa to keep precision
      LZCNT = _lzcnt_u64(tmp_mantissa);
      tmp_mantissa = tmp_mantissa << LZCNT;
    }
  }

  if (sci_exp < 0){
    // Shift mantissa to keep precision
    LZCNT = _lzcnt_u64(tmp_mantissa);
    tmp_mantissa = tmp_mantissa << LZCNT;
    for (int i=sci_exp; i<0; i++){
      tmp_mantissa /= 5;
      // Shift mantissa to keep precision
      if (i%16 == 0){
        LZCNT = _lzcnt_u64(tmp_mantissa);
        tmp_mantissa = tmp_mantissa << LZCNT;
        exp_adjustment -= LZCNT;
      }
    }
    exp_adjustment += -_lzcnt_u64(tmp_mantissa) + sci_exp;
  } else if (sci_exp > 0){
    // TODO: Handle exp_adjustment
    perror("sci_exp > 0");
    exit(-1);
    for (int i=0; i<sci_exp; i++){
      tmp_mantissa *= 5;
    }
  }

  // Clear highest set bit, since it is implicit
  LZCNT = _lzcnt_u64(tmp_mantissa);
  tmp_mantissa = tmp_mantissa << (LZCNT+1);
  mantissa = (uint32_t)(tmp_mantissa >> (32+9));
  // What exponent to chose depending on leading digit. 0 is handled separatlely, should never be accessed.
  int8_t exps[10] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3};
  exp = exps[buf[leading_zs] - '0'];
  if (sci_exp != 0){
    exp += exp_adjustment;
  }

#define CREATE_SIGN_BIT_F32(sign) (((uint32_t)(sign))<<31)
#define CREATE_EXPONENT_F32(exp) (((uint32_t)(((exp)+127)&0xFF))<<23)
#define CREATE_MANTISSA_F32(val) (((uint32_t)(val)) & 0x7FFFFF)
  uint32_t res = CREATE_SIGN_BIT_F32(sign)
               | CREATE_EXPONENT_F32(exp)
               | CREATE_MANTISSA_F32(mantissa);
  float *fans = (float *)&res;
  return *fans;
}

#endif // __PARSE_IMPL
