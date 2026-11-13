/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GUARDIAN_CID_H
#define GUARDIAN_CID_H
#include <stddef.h>
/* Physical CID bytes returned by Hekate: four 32-bit response words in native
 * little-endian memory. No device identity field may be omitted or wildcarded. */
void guardian_cid_hex(char out[33],const unsigned char bytes[16]);
/* Convert Hekate's shifted R2 cache back into SDHCI response register bytes,
 * which the SDK returns through GetSdCardCid. No CRC guessing or bit masking. */
int guardian_cid_sdk_bytes(unsigned char out[16],const unsigned char physical[16]);
/* 0 mismatch/malformed, 1 exact raw/order match, 2 exact SDK register match. */
int guardian_cid_match(const unsigned char physical[16],const char *expected,char matched[33]);
#endif
