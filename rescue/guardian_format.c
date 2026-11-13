/* SPDX-License-Identifier: GPL-2.0-only
 * Bounded integer/string formatter for the freestanding rescue environment.
 * No allocation, locale, stdio, floating point, or hidden newlib heap.
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
typedef struct { char *b; size_t cap,n; } outbuf;
static void put(outbuf *o,char c) { if (o->cap && o->n<o->cap-1) o->b[o->n]=c; o->n++; }
int guardian_snprintf(char *buffer,size_t cap,const char *fmt,...) {
    outbuf o={buffer,cap,0}; va_list ap; va_start(ap,fmt);
    while (*fmt) {
        if (*fmt!='%') { put(&o,*fmt++); continue; }
        fmt++;
        if (*fmt=='%') { put(&o,*fmt++); continue; }
        int zero=0,width=0,precision=-1,length=0;
        if (*fmt=='0') { zero=1; fmt++; }
        while (*fmt>='0' && *fmt<='9') { if (width<1000) width=width*10+*fmt-'0'; fmt++; }
        if (*fmt=='.') { precision=0; fmt++; while (*fmt>='0' && *fmt<='9') { if (precision<1000000) precision=precision*10+*fmt-'0'; fmt++; } }
        while (*fmt=='l') { length++; fmt++; }
        char type=*fmt; if (!type) { va_end(ap); return -1; } fmt++;
        if (type=='s') {
            const char *s=va_arg(ap,const char *); size_t n=0; if (!s) s="(null)";
            while (s[n] && (precision<0 || n<(size_t)precision)) n++;
            for (int j=(int)n;j<width;j++) put(&o,' ');
            for (size_t j=0;j<n;j++) put(&o,s[j]);
        } else if (type=='u' || type=='d' || type=='x' || type=='X') {
            uint64_t n; int negative=0; char digits[32]; unsigned count=0,base=(type=='x'||type=='X')?16:10;
            if (type=='d') {
                int64_t signed_n=length>=2?va_arg(ap,long long):length?va_arg(ap,long):va_arg(ap,int);
                negative=signed_n<0; n=negative?(uint64_t)(-(signed_n+1))+1:(uint64_t)signed_n;
            } else n=length>=2?va_arg(ap,unsigned long long):length?va_arg(ap,unsigned long):va_arg(ap,unsigned int);
            do { unsigned v=(unsigned)(n%base); digits[count++]=v<10?'0'+v:(type=='X'?'A':'a')+v-10; n/=base; } while (n);
            if (negative && zero) put(&o,'-');
            for (int j=(int)count+negative;j<width;j++) put(&o,zero?'0':' ');
            if (negative && !zero) put(&o,'-');
            while (count) put(&o,digits[--count]);
        } else if (type=='c') put(&o,(char)va_arg(ap,int));
        else { va_end(ap); if (cap) buffer[o.n<cap?o.n:cap-1]=0; return -1; }
    }
    va_end(ap); if (cap) buffer[o.n<cap?o.n:cap-1]=0;
    return o.n>INT_MAX?-1:(int)o.n;
}
