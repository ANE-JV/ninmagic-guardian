/* SPDX-License-Identifier: GPL-2.0-only
 * Hekate shifts the SDHCI R2 response register words left eight bits:
 * bdk/storage/sdmmc_driver.c, _sdmmc_cache_rsp(), pinned commit 8e1392df.
 * Atmosphere's SdHostStandardController::GetResponse(R2) copies the four
 * registers directly and BaseDevice::SetCid/GetCid preserves those bytes.
 * Invert Hekate's register transform exactly, including the fixed unused byte
 * in R3. Every identity comparison below still compares all 16 SDK bytes.
 */
#include "guardian_cid.h"
#include <string.h>
#include <stdint.h>
void guardian_cid_hex(char out[33],const unsigned char bytes[16]) {
    static const char h[]="0123456789abcdef";
    for (unsigned j=0;j<16;j++) { out[j*2]=h[bytes[j]>>4]; out[j*2+1]=h[bytes[j]&15]; }
    out[32]=0;
}
static uint32_t le32(const unsigned char *b) {
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
int guardian_cid_sdk_bytes(unsigned char out[16],const unsigned char physical[16]) {
    uint32_t h[4],r[4]; unsigned any=0;
    if (!out || !physical) return 0;
    for (unsigned j=0;j<4;j++) { h[j]=le32(physical+j*4); any|=h[j]; }
    if (!any || (h[3]&0xffU)) return 0;
    r[0]=(h[3]>>8)|(h[2]<<24);
    r[1]=(h[2]>>8)|(h[1]<<24);
    r[2]=(h[1]>>8)|(h[0]<<24);
    r[3]=h[0]>>8;
    for (unsigned j=0;j<4;j++) for (unsigned k=0;k<4;k++) out[j*4+k]=(unsigned char)(r[j]>>(k*8));
    return 1;
}
static void reorder(unsigned char out[16],const unsigned char in[16],unsigned mode) {
    for (unsigned j=0;j<16;j++) {
        unsigned k=j;
        if (mode&1) k=(3-(k/4))*4+(k%4);
        if (mode&2) k=(k/4)*4+(3-(k%4));
        out[j]=in[k];
    }
}
static int value(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
int guardian_cid_match(const unsigned char physical[16],const char *expected,char matched[33]) {
    unsigned char wanted[16],candidate[16]; unsigned any=0;
    if (matched) matched[0]=0;
    if (!physical || !expected || strlen(expected)!=32) return 0;
    for (unsigned j=0;j<16;j++) {
        int a=value(expected[j*2]),b=value(expected[j*2+1]);
        if (a<0 || b<0) return 0;
        wanted[j]=(unsigned char)((a<<4)|b); any|=wanted[j];
    }
    if (!any) return 0;
    for (unsigned mode=0;mode<4;mode++) {
        reorder(candidate,physical,mode);
        if (!memcmp(candidate,wanted,16)) { if(matched) guardian_cid_hex(matched,wanted); return 1; }
    }
    if (guardian_cid_sdk_bytes(candidate,physical) && !memcmp(candidate,wanted,16)) {
        if(matched) guardian_cid_hex(matched,wanted);
        return 2;
    }
    return 0;
}
