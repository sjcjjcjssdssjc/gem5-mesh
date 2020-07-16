#include "util.h"
#include <stdlib.h>

// use this to chunk data among vector groups
// https://stackoverflow.com/questions/3407012/c-rounding-up-to-the-nearest-multiple-of-a-number
int roundUp(int numToRound, int multiple) {
  if (multiple == 0) {
    return numToRound;
  }

  int remainder = abs(numToRound) % multiple;
  if (remainder == 0) {
    return numToRound;
  }

  if (numToRound < 0) {
    return -(abs(numToRound) - remainder);
  }
  else {
    return numToRound + multiple - remainder;
  }
}

// ret 0 if not within eps diff
// ret 1 if within eps diff
int float_compare(float a, float b, float allowed_perc_diff) {
  float diff = b - a;
  if (diff < 0.0f)
    diff *= -1.0f;

  float perc_diff = diff / b;

  if (perc_diff < allowed_perc_diff) return 1;
  else return 0;
}