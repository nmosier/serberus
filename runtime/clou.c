#include <stddef.h>

// TODO: Get rid of this
#ifndef min
#define min(a, b) ((a) < (b) : (a) : (b))
#endif

#define BLKSIZE 4096

// hardened memcpy?

void clou_transform_xor(char *dst, const char *src1, const char *src2, size_t len) {
  /* Algorithm: 
   * Speculative load hardening on each iteration.
   */

  size_t i = 0;
  asm volatile ("jmp .entry.%=\n"
		".loop.%=:\n"
		/* Reset the index if the branch is mispredicted. */
		"  cmove %[i], %[zero]\n"
		"  mov al, [%[src1] + %[i]]\n"
		"  xor al, [%[src2] + %[i]]\n"
		"  mov [%[dst] + %[i]], al\n"
		"  inc %[i]\n"
		".entry.%=:\n"
		"  cmp %[i], %[len]\n"
		"  jne .loop.%=\n"
		: [i] "+r" (i)
		: [dst] "r" (dst),
		  [src1] "r" (src1),
		  [src2] "r" (src2),
		  [len] "r" (len),
		  [zero] "r" (0ULL)
		: "cc", "memory", "al");
}
