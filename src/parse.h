#ifndef __PARSE_H
#define __PARSE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <x86gprintrin.h>
#include "util.h"

INLINE char *parse_line(char *str, float *data);
INLINE float parse_float(char *restrict str, char **restrict end);

#endif // __PARSE_H

#ifdef PARSE_IMPLEMENTATION
/*
  Parse a float with one integer digit, possibly with scientific notation.
  The last bit in the mantissa might differ from when using `strtof` because
  different roundings are used (`parse_float` truncates, `strtof` doesn't).
  The parser requires support for 64 bit integers and the `lzcnt` instruction.
  NOTE: Can only parse floats in the range (-10,10)
*/
INLINE float parse_float(char *restrict orig_str, char **restrict end){
  bool sign = false;
  char *str = orig_str;
  if (*str == '-'){
    sign = true;
    str++;
  }
  // Initialize num to first digit in string
  uint64_t num = *str-'0';
  str++;
  // Skip decimal point
  if (*str != '.'){
    printf("%.32s\n", str);
    perror("*str != '.'");
    exit(-1);
  }
  str++;

  int ndecimals = 0;
  const int max_digits = 18;
  // 17 digits is what is needed to exactly parse a float? Fits in 64 bits.
  for (int i=1; i<max_digits; i++){
    if (*str>'9' || *str<'0'){
      break;
    }
    num = num*10 + *str - '0';
    ndecimals++;
    str++;
  }
  // Skip redundant decimals
  while (*str >= '0' && *str <= '9'){
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
  if (num == 0){
    return 0;
  }

  // Count leading zeros in buf. Reinterpret as scientific notation.
  int leading_zs = 0;
  // The first non zero digit in the number
  int fst_nzd = 0;
  for (int i=sign; i<ndecimals+1; i++){
    if (orig_str[i] > '0'){
      fst_nzd = orig_str[i]-'0';
      break;
    }
    if (orig_str[i] == '0'){
      leading_zs++;
    }
  }
  sci_exp -= leading_zs;
  ndecimals -= leading_zs;

  int8_t exp = 0;
  uint32_t mantissa = 0;
  int LZCNT = 0;
  uint64_t tmp_mantissa = num;
  for (int i=0; i<ndecimals; i++){
    if (i%16 == 0){
      // Shift mantissa to keep precision
      LZCNT = _lzcnt_u64(tmp_mantissa);
      tmp_mantissa = tmp_mantissa << LZCNT;
    }
    // Divide by 5 and later adjust for the factor 2's in exponent
    tmp_mantissa /= 5;
  }

  if (sci_exp < 0){
    for (int i=0; i<-sci_exp; i++){
      if (i%16 == 0){
        // Shift mantissa to keep precision
        LZCNT = _lzcnt_u64(tmp_mantissa);
        tmp_mantissa = tmp_mantissa << LZCNT;
        if (i != 0){
          exp -= LZCNT;
        }
      }
      tmp_mantissa /= 5;
    }
    exp += -_lzcnt_u64(tmp_mantissa) + sci_exp;
  } else if (sci_exp > 0){
    // TODO: Handle adjustments to exp
    perror("sci_exp > 0");
    exit(-1);
    for (int i=0; i<sci_exp; i++){
      tmp_mantissa *= 5;
    }
  }

  // Shift mantissa to correct alignment
  LZCNT = _lzcnt_u64(tmp_mantissa);
  mantissa = (uint32_t)(tmp_mantissa >> (32+9-LZCNT-1));
  // What exponent to choose depending on leading digit. 0 should never be accessed.
  int8_t exps[10] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3};
  exp += exps[fst_nzd];

#define CREATE_SIGN_BIT_F32(sign) (((uint32_t)(sign))<<31)
#define CREATE_EXPONENT_F32(exp) (((uint32_t)(((exp)+127)&0xFF))<<23)
#define CREATE_MANTISSA_F32(val) (((uint32_t)(val)) & 0x7FFFFF)
  uint32_t res = CREATE_SIGN_BIT_F32(sign)
               | CREATE_EXPONENT_F32(exp)
               | CREATE_MANTISSA_F32(mantissa);
  float *fans = (float *)&res;
  return *fans;
}


/*
  Parses 3 floats separated by a single space.
  NOTE: Assumes that the first char is the start of the float
 */
INLINE char *parse_line(char *str, float *data){
  char *end = NULL;
  #pragma GCC unroll 3
  for (int i=0; i<3; i++){
    *data = parse_float(str, &end);
    str = end+1;
    data++;
  }
  // Skip nexline to get to beginning of the next line
  while (*str<' ' && *str>'\0'){
    str++;
  }
  return str;
}
#endif // PARSE_IMPLEMENTATION
