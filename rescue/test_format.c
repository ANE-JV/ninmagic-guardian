/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
int guardian_snprintf(char*,size_t,const char*,...);
int main(void) {
    char a[512],b[512],tiny[5]; int x,y;
    x=guardian_snprintf(a,sizeof a,"F|%llu|%s|%s\n",18446744073709551615ULL,"abc012","atmosphere/package3");
    y=snprintf(b,sizeof b,"F|%llu|%s|%s\n",18446744073709551615ULL,"abc012","atmosphere/package3");
    assert(x==y && !strcmp(a,b));
    x=guardian_snprintf(a,sizeof a,"plans/%04u.txt|%u|%.3s|%d|%08x",7U,4096U,"abcdef",-123,0x12abU);
    y=snprintf(b,sizeof b,"plans/%04u.txt|%u|%.3s|%d|%08x",7U,4096U,"abcdef",-123,0x12abU);
    assert(x==y && !strcmp(a,b));
    assert(guardian_snprintf(tiny,sizeof tiny,"%s","123456789")==9 && !strcmp(tiny,"1234"));
    assert(guardian_snprintf(NULL,0,"%llu",9000000001ULL)==10);
    assert(guardian_snprintf(a,sizeof a,"%.0s%%%c","abc",'Q')==2 && !strcmp(a,"%Q"));
    assert(guardian_snprintf(a,sizeof a,"%f",1.0)==-1);
    return 0;
}
