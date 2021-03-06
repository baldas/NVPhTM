#include <assert.h>
#include <stdlib.h> // erand48

#include <common.h>

void
rand_init(unsigned short *seed)
{
  seed[0] = (unsigned short)rand();
  seed[1] = (unsigned short)rand();
  seed[2] = (unsigned short)rand();
}

int
rand_range(int n, unsigned short *seed)
{
  /* Return a random number in range [0;n) */
  int v = (int)(erand48(seed) * n);
  assert (v >= 0 && v < n);
  return v;
}

int
rand_range_non_zero(int n, unsigned short *seed)
{
  /* Return a random number in range [1;n) */
	int v = rand_range(n,seed);
	return (v == 0 ? 1 : v);
}
