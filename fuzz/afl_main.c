/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <stdint.h>
#include <stdio.h>
int LLVMFuzzerTestOneInput(const uint8_t *,size_t);
int main(void)
{
 uint8_t input[65536];
 size_t n=fread(input,1,sizeof(input),stdin);
 LLVMFuzzerTestOneInput(input,n);
 return 0;
}
