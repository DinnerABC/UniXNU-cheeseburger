/* SPDX-License-Identifier: MIT */
#include "Library.h"
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[3] | (uint32_t)p[2]<<8 | (uint32_t)p[1]<<16 | (uint32_t)p[0]<<24;
}
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p+4)<<32; }
static int inside(size_t at, size_t n, size_t limit) { return at<=limit && n<=limit-at; }
static int overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn) {
    if (an>UINTPTR_MAX-a || bn>UINTPTR_MAX-b) return 1;
    return an && bn && a<b+bn && b<a+an;
}
static int equal(const uint8_t *a, const uint8_t *b, size_t n) {
    unsigned difference=0;
    for (size_t i=0;i<n;i++) difference|=a[i]^b[i];
    return !difference;
}
static int profile(const uint8_t *p, size_t n) {
    static const uint8_t magic[16]={0x4d,0x54,0x4c,0x42,1,0,2,0,7,0,0,0x82,0x11,0,7,0};
    return n>=88 && equal(p,magic,16);
}
static uint32_t select_slice(const uint8_t *p, size_t n, struct air_library_view *v) {
    size_t offset=0,size=n;
    if (n>=8 && be32(p)==0xcafebabe) {
        uint32_t count=be32(p+4),selected=0;
        if (!count || count>16 || !inside(8,(size_t)count*20,n)) return AIR_LIBRARY_FORMAT;
        size_t previous=8+(size_t)count*20;
        for (uint32_t i=0;i<count;i++) {
            const uint8_t *a=p+8+(size_t)i*20;
            size_t at=be32(a+8),bytes=be32(a+12);uint32_t align=be32(a+16);
            if (align>20 || at%((size_t)1<<align) || at<previous || !bytes || !inside(at,bytes,n)) return AIR_LIBRARY_BOUNDS;
            previous=at+bytes;
            if (profile(p+at,bytes)) { offset=at;size=bytes;selected++; }
        }
        if (selected!=1) return AIR_LIBRARY_FORMAT;
    }
    const uint8_t *s=p+offset;
    if (!profile(s,size) || le64(s+16)!=size) return AIR_LIBRARY_FORMAT;
    size_t section[4][2];
    for (unsigned i=0;i<4;i++) {
        uint64_t at=le64(s+24+i*16),bytes=le64(s+32+i*16);
        if (at>size || bytes>size-at || at<88) return AIR_LIBRARY_BOUNDS;
        section[i][0]=(size_t)at;section[i][1]=(size_t)bytes;
        if (i && section[i-1][0]+section[i-1][1]>at) return AIR_LIBRARY_BOUNDS;
    }
    size_t begin=section[0][0],bytes=section[0][1];
    if (!inside(begin,4,size) || !inside(begin+4,bytes,size)) return AIR_LIBRARY_BOUNDS;
    size_t end=begin+4+bytes;
    if (!inside(end,48,size) || end+48!=section[1][0]) return AIR_LIBRARY_FORMAT;
    const uint8_t *trailer=s+end;
    static const uint8_t rl[6]={'R','L','S','T',16,0}, uuid[6]={'U','U','I','D',16,0};
    if (!equal(trailer,rl,6) || !equal(trailer+22,uuid,6) || le32(trailer+44)!=0x54444e45) return AIR_LIBRARY_FORMAT;
    uint64_t reflection=le64(trailer+6),length=le64(trailer+14);
    if (reflection!=section[3][0]+section[3][1] || reflection>size || length!=size-reflection) return AIR_LIBRARY_BOUNDS;
    uint32_t count=le32(s+begin);
    if (!count || count>AIR_LIBRARY_MAX_FUNCTIONS || (size_t)count>bytes/8) return AIR_LIBRARY_FORMAT;
    *v=(struct air_library_view){p,n,offset,size,begin+4,bytes,count,0};
    return AIR_LIBRARY_OK;
}
static uint32_t record(const struct air_library_view *v, size_t *cursor, uint32_t index, struct air_library_function *out) {
    const uint8_t *s=v->data+v->slice_offset;
    size_t end=v->functions_offset+v->functions_bytes,at=*cursor;
    if (!inside(at,4,end)) return AIR_LIBRARY_BOUNDS;
    size_t size=le32(s+at);
    if (size<8 || !inside(at,size,end)) return AIR_LIBRARY_BOUNDS;
    end=at+size;at+=4;
    const uint8_t *name=0,*type=0,*hash=0,*offsets=0,*module_size=0;
    size_t name_bytes=0;uint32_t seen[16];unsigned tags=0;int terminated=0;
    while (at<end) {
        if (!inside(at,4,end)) return AIR_LIBRARY_BOUNDS;
        uint32_t tag=le32(s+at);at+=4;
        if (tag==0x54444e45) { terminated=at==end;break; }
        if (tags==16 || !inside(at,2,end)) return AIR_LIBRARY_FORMAT;
        for (unsigned i=0;i<tags;i++) if (seen[i]==tag) return AIR_LIBRARY_FORMAT;
        seen[tags++]=tag;
        size_t bytes=s[at]|(size_t)s[at+1]<<8;at+=2;
        if (!inside(at,bytes,end)) return AIR_LIBRARY_BOUNDS;
        const uint8_t *value=s+at;at+=bytes;
        switch (tag) {
        case 0x454d414e: name=value;name_bytes=bytes;break; /* NAME */
        case 0x45505954: if (bytes!=1) return AIR_LIBRARY_FORMAT;type=value;break;
        case 0x48534148: if (bytes!=32) return AIR_LIBRARY_FORMAT;hash=value;break;
        case 0x5446464f: if (bytes!=24) return AIR_LIBRARY_FORMAT;offsets=value;break;
        case 0x5a53444d: if (bytes!=8) return AIR_LIBRARY_FORMAT;module_size=value;break;
        default:break;
        }
    }
    if (!terminated || !name || !type || !hash || !offsets || !module_size || name_bytes<2 || name_bytes>1024 || name[name_bytes-1]) return AIR_LIBRARY_FORMAT;
    for (size_t i=0;i+1<name_bytes;i++) if (name[i]<32 || name[i]>126) return AIR_LIBRARY_FORMAT;
    if (*type!=0 && *type!=1 && *type!=2 && *type!=4) return AIR_LIBRARY_FORMAT;
    uint64_t public=le64(offsets),private=le64(offsets+8),module=le64(offsets+16),bytes=le64(module_size);
    uint64_t public_bytes=le64(s+48),private_bytes=le64(s+64),code_begin=le64(s+72),code_bytes=le64(s+80);
    if (public>=public_bytes || private>=private_bytes || module>code_bytes || bytes<20 || bytes>code_bytes-module) return AIR_LIBRARY_BOUNDS;
    const uint8_t *wrapper=s+(size_t)(code_begin+module);
    if (le32(wrapper)!=0x0b17c0de || le32(wrapper+4)!=0 || le32(wrapper+16)!=0xffffffff) return AIR_LIBRARY_FORMAT;
    size_t bc=le32(wrapper+8),bc_bytes=le32(wrapper+12);
    if (bc<20 || bc_bytes<=4 || !inside(bc,bc_bytes,(size_t)bytes)) return AIR_LIBRARY_BOUNDS;
    if (le32(wrapper+bc)!=0xdec04342) return AIR_LIBRARY_FORMAT;
    *out=(struct air_library_function){name,wrapper+bc,wrapper,hash,name_bytes-1,bc_bytes,(size_t)bytes,index,*type};
    *cursor=end;
    return AIR_LIBRARY_OK;
}
uint32_t air_library_open(const uint8_t *p, size_t n, air_library_sha256 hash, void *context, struct air_library_view *out) {
    if (!p || !out || !hash || !n || n>AIR_LIBRARY_MAX_BYTES || n>UINTPTR_MAX-(uintptr_t)p) return AIR_LIBRARY_ARGUMENT;
    if (overlap((uintptr_t)p,n,(uintptr_t)out,sizeof(*out))) return AIR_LIBRARY_ALIAS;
    struct air_library_view v;uint32_t error=select_slice(p,n,&v);
    if (error) return error;
    size_t cursor=v.functions_offset;const uint8_t *previous=0;size_t previous_bytes=0;const uint8_t *previous_hash=0;
    for (uint32_t i=0;i<v.count;i++) {
        struct air_library_function f;error=record(&v,&cursor,i,&f);
        if (error) return error;
        if (previous && f.wrapper<previous+previous_bytes && !(f.wrapper==previous && f.wrapper_bytes==previous_bytes)) return AIR_LIBRARY_BOUNDS;
        if (previous==f.wrapper && previous_bytes==f.wrapper_bytes) {
            if (!equal(previous_hash,f.wrapper_sha256,32)) return AIR_LIBRARY_HASH;
        } else {
            uint8_t digest[32];
            if (hash(f.wrapper,f.wrapper_bytes,digest,context) || !equal(digest,f.wrapper_sha256,32)) return AIR_LIBRARY_HASH;
        }
        previous=f.wrapper;previous_bytes=f.wrapper_bytes;previous_hash=f.wrapper_sha256;
    }
    if (cursor!=v.functions_offset+v.functions_bytes) return AIR_LIBRARY_FORMAT;
    *out=v;
    return AIR_LIBRARY_OK;
}
uint32_t air_library_get(const struct air_library_view *v, uint32_t index, struct air_library_function *out) {
    if (!v || !out || !v->data || !v->bytes || v->bytes>AIR_LIBRARY_MAX_BYTES || v->bytes>UINTPTR_MAX-(uintptr_t)v->data) return AIR_LIBRARY_ARGUMENT;
    if (overlap((uintptr_t)v,sizeof(*v),(uintptr_t)out,sizeof(*out)) || overlap((uintptr_t)v->data,v->bytes,(uintptr_t)out,sizeof(*out))) return AIR_LIBRARY_ALIAS;
    struct air_library_view actual;uint32_t error=select_slice(v->data,v->bytes,&actual);
    if (error) return error;
    if (actual.slice_offset!=v->slice_offset || actual.slice_bytes!=v->slice_bytes || actual.functions_offset!=v->functions_offset || actual.functions_bytes!=v->functions_bytes || actual.count!=v->count || v->reserved) return AIR_LIBRARY_FORMAT;
    if (index>=v->count) return AIR_LIBRARY_INDEX;
    size_t cursor=v->functions_offset;
    for (uint32_t i=0;i<=index;i++) {
        struct air_library_function f;error=record(v,&cursor,i,&f);
        if (error) return error;
        if (i==index) { *out=f;return AIR_LIBRARY_OK; }
    }
    return AIR_LIBRARY_INDEX;
}
