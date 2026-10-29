// Studio 0.744 adapter kernels. No imports; callbacks use the original Studio routines.
using uint64_t=unsigned __int64;
using uint32_t=unsigned int;
using size_t=unsigned __int64;
extern "C" void* memcpy(void*,const void*,size_t);
extern "C" { int _fltused=0; }
#pragma intrinsic(memcpy)
using byte=unsigned char;
template<class T> static T get(const void* p, size_t off=0) { T v; memcpy(&v,(const byte*)p+off,sizeof(v)); return v; }
template<class T> static void put(void* p,size_t off,T v) { memcpy((byte*)p+off,&v,sizeof(v)); }
using ReallocFn=void*(*)(void*,void*,size_t,size_t,byte);
struct FillValue { uint64_t value, tag; };
extern "C" void KernelCreate(void* L,void* table,int count,ReallocFn allocate) {
    // The dispatch roots an empty table using original lua_createtable first.
    // Snapshot the rooted argument before invoking the original array allocator.
    const FillValue fill=get<FillValue>(get<byte*>(L,0x58),16);
    // Original luaM_reallocarray throws on positive-size allocation failure;
    // its contract does not permit a normal null return.
    byte* array=(byte*)allocate(L,nullptr,0,size_t(count)*16,get<byte>(table,2));
    int i=0;
    for(;i+4<=count;i+=4) {
        put(array,size_t(i)*16,fill);put(array,size_t(i+1)*16,fill);
        put(array,size_t(i+2)*16,fill);put(array,size_t(i+3)*16,fill);
    }
    for(;i<count;i++) put(array,size_t(i)*16,fill);
    // Allocation failure leaves the rooted empty table valid for collection.
    put(table,0x18,array);put(table,8,count);
}
using CaseFn=int(*)(int);
template<bool upper> static void caseMap(byte* dst,const byte* src,size_t len,CaseFn crt) {
    size_t i=0;
    bool standard=len>=4096;
    if(standard) for(int c=0;c<128;c++) {
        const int expected=upper ? ((c>='a'&&c<='z')?c-32:c) : ((c>='A'&&c<='Z')?c+32:c);
        if(crt(c)!=expected) {standard=false;break;}
    }
    if(standard) for(;i+8<=len;i+=8) {
        uint64_t x=get<uint64_t>(src+i);
        if(x&0x8080808080808080ull) break;
        constexpr uint64_t ones=0x0101010101010101ull, hi=0x8080808080808080ull;
        constexpr int first=upper?'a':'A', last=upper?'z':'Z';
        const uint64_t mask=((x+ones*(0x80-first))&hi)&~((x+ones*(0x80-last-1))&hi);
        if(upper) x&=~(mask>>2); else x|=mask>>2;
        put(dst+i,0,x);
    }
    for(;i<len;i++) dst[i]=(byte)crt(src[i]);
}
extern "C" void KernelLower(byte* d,const byte* s,size_t n,CaseFn fn){caseMap<false>(d,s,n,fn);}
extern "C" void KernelUpper(byte* d,const byte* s,size_t n,CaseFn fn){caseMap<true>(d,s,n,fn);}
using Original=int(*)(void*);
static bool integer(const byte* value,uint64_t maximum,uint64_t& result) {
    if(get<int>(value,12)!=3) return false;
    double x=get<double>(value);
    // Explicit ordered comparisons reject NaN and infinities before conversion.
    if(!(x>=0.0 && x<=double(maximum))) return false;
    result=(uint64_t)x;
    return double(result)==x;
}
template<bool write> static int bits(void* L,Original fallback) {
    byte* base=get<byte*>(L,0x58); byte* top=get<byte*>(L,0x70);
    if(top<base || size_t(top-base)<(write?64:48) || get<int>(base,12)!=11) return fallback(L);
    uint64_t off,count,value=0;
    if(!integer(base+16,9007199254740991ull,off) || !integer(base+32,32,count) ||
       (write&&!integer(base+48,4294967295ull,value))) return fallback(L);
    byte* buffer=get<byte*>(base);
    const uint64_t len=get<uint32_t>(buffer,4);
    if(off+count>len*8) return fallback(L);
    if(!write) {
        byte* ci=get<byte*>(L,0x50);
        if(top+16>get<byte*>(ci,0x18)) return fallback(L);
    }
    const uint64_t start=off>>3,shift=off&7,nbytes=(shift+count+7)>>3;
    uint64_t word=0;
    // At count 0 with an unaligned offset, exact path still reads one valid byte.
    if(!write && start+8<=len) word=get<uint64_t>(buffer+8+start);
    else for(uint64_t i=0;i<nbytes;i++) word|=uint64_t(buffer[8+start+i])<<(i*8);
    const uint64_t mask=count?((uint64_t(1)<<count)-1):0;
    if(write) {
        word=(word&~(mask<<shift))|((value&mask)<<shift);
        for(uint64_t i=0;i<nbytes;i++) buffer[8+start+i]=byte(word>>(i*8));
        return 0;
    }
    const double result=double((word>>shift)&mask);
    put(top,0,result);put(top,12,3);put(L,0x70,top+16);
    return 1;
}
extern "C" int KernelReadBits(void* L,Original fallback){return bits<false>(L,fallback);}
extern "C" int KernelWriteBits(void* L,Original fallback){return bits<true>(L,fallback);}

// Constant-space Two-Way search: critical factorization followed by periodic/nonperiodic scans.
// Algorithm reference: musl src/string/memmem.c (MIT), https://git.musl-libc.org/cgit/musl/tree/src/string/memmem.c
// Constant-space search keeps the native non-allocating interface.
static size_t suffix(const byte* n,size_t len,size_t& period,bool reverse) {
    size_t a=size_t(-1),b=0,k=1;period=1;
    while(b+k<len) {
        const byte x=n[a+k],y=n[b+k];
        if(x==y) {if(k==period){b+=period;k=1;}else ++k;}
        else if(reverse?x<y:x>y){b+=k;k=1;period=b-a;}
        else {a=b++;k=period=1;}
    }
    return a;
}
extern "C" const byte* KernelSearch(const byte* h,const byte* n,size_t size,size_t len) {
    if(!len)return h;if(size<len)return nullptr;
    size_t p,q,a=suffix(n,len,p,false),b=suffix(n,len,q,true);
    if(b+1>a+1){a=b;p=q;}
    size_t cut=a+1,keep=0;
    bool periodic=true;
    for(size_t i=0;i<cut;i++)if(n[i]!=n[i+p]){periodic=false;break;}
    if(periodic)keep=len-p;else p=(a>len-a-1?a:len-a-1)+1;
    size_t pos=0,memory=0;
    while(pos<=size-len) {
        size_t i=cut>memory?cut:memory;
        while(i<len&&n[i]==h[pos+i])++i;
        if(i<len){pos+=i-a;memory=0;continue;}
        i=cut;while(i>memory&&n[i-1]==h[pos+i-1])--i;
        if(i<=memory)return h+pos;
        pos+=p;memory=keep;
    }
    return nullptr;
}
