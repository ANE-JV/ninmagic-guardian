/* SPDX-License-Identifier: GPL-2.0-only */
#include "guardian_cid.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static void reorder(unsigned char out[16],const unsigned char in[16],unsigned mode) {
    for (unsigned j=0;j<16;j++) { unsigned k=j;if(mode&1)k=(3-k/4)*4+k%4;if(mode&2)k=(k/4)*4+3-k%4;out[j]=in[k]; }
}
static void store(unsigned char *b,uint32_t value) {for(unsigned j=0;j<4;j++)b[j]=(unsigned char)(value>>(j*8));}
int main(int argc,char **argv) {
    const unsigned char physical[16]={0x13,0xac,0x68,0x24,0x9a,0xdf,0x9b,0x57,0x12,0xf0,0xde,0xbc,0x00,0x78,0x56,0x34};
    const unsigned char sdk[16]={0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,0xdf,0x9b,0x57,0x13,0xac,0x68,0x24,0x00};
    unsigned char candidate[16],other[16],out[16];char expected[33],matched[33];uint32_t seed=1;
    for (unsigned mode=0;mode<4;mode++) {
        reorder(candidate,physical,mode);guardian_cid_hex(expected,candidate);
        assert(guardian_cid_match(physical,expected,matched)==1 && !strcmp(expected,matched));
        assert(memcmp(candidate,sdk,16)); /* All old permutations miss SDK form. */
    }
    assert(guardian_cid_sdk_bytes(out,physical) && !memcmp(out,sdk,16));
    guardian_cid_hex(expected,sdk);
    assert(guardian_cid_match(physical,expected,matched)==2 && !strcmp(expected,matched));
    for (unsigned j=0;j<16;j++) {memcpy(other,sdk,16);other[j]^=1;guardian_cid_hex(expected,other);assert(!guardian_cid_match(physical,expected,matched));}
    for (unsigned test=0;test<1000;test++) {
        uint32_t r[4],h[4];unsigned char raw[16],want[16];
        for(unsigned j=0;j<4;j++){seed=seed*1664525U+1013904223U;r[j]=seed;}r[3]&=0x00ffffffU;
        h[0]=(r[3]<<8)|(r[2]>>24);h[1]=(r[2]<<8)|(r[1]>>24);h[2]=(r[1]<<8)|(r[0]>>24);h[3]=r[0]<<8;
        for(unsigned j=0;j<4;j++){store(raw+j*4,h[j]);store(want+j*4,r[j]);}
        assert(guardian_cid_sdk_bytes(out,raw) && !memcmp(out,want,16));
        guardian_cid_hex(expected,want);assert(guardian_cid_match(raw,expected,matched));
    }
    assert(!guardian_cid_match(physical,"00000000000000000000000000000000",matched));
    assert(!guardian_cid_match(physical,"zz000000000000000000000000000000",matched));
    assert(!guardian_cid_match(physical,"03",matched));
    /* Optional PRIVATE fixture check. Values stay in memory and are never
     * printed or embedded in public source; argv is a manifest path. */
    if (argc==2) {
        FILE *in=fopen(argv[1],"rb");char line[1024],identity[33]={0};
        uint32_t r[4]={0},h[4];unsigned char raw[16],sdk_bytes[16];
        assert(in);
        while(fgets(line,sizeof line,in)) if(!strncmp(line,"identity=",9)){memcpy(identity,line+9,32);break;}
        assert(!fclose(in) && strlen(identity)==32);
        for(unsigned j=0;j<16;j++){char pair[3]={identity[j*2],identity[j*2+1],0};sdk_bytes[j]=(unsigned char)strtoul(pair,NULL,16);}
        for(unsigned j=0;j<4;j++)for(unsigned k=0;k<4;k++)r[j]|=(uint32_t)sdk_bytes[j*4+k]<<(k*8);
        assert(!(r[3]&0xff000000U));
        h[0]=(r[3]<<8)|(r[2]>>24);h[1]=(r[2]<<8)|(r[1]>>24);h[2]=(r[1]<<8)|(r[0]>>24);h[3]=r[0]<<8;
        for(unsigned j=0;j<4;j++)store(raw+j*4,h[j]);
        assert(guardian_cid_match(raw,identity,matched)==2);
        for(unsigned mode=0;mode<4;mode++){reorder(candidate,raw,mode);assert(memcmp(candidate,sdk_bytes,16));}
        puts("PASS: private SDK CID roundtrip exact; old permutations reject; no identity value printed");
    }
    return 0;
}
