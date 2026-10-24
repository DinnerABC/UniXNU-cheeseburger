#!/usr/bin/env python3
"""Host test actual upstream-derived formatter and Darwin panic wrappers.
Privileged address translation and display hardware are replaced by fixtures.
"""
from pathlib import Path
import re,subprocess,json,hashlib
r=Path(__file__).resolve().parents[1];o=r/'out/p9/host-test';o.mkdir(exist_ok=True);render=(r/'drivers/p9/Renderer.inc').read_text()
a=render.index('static int readable (');b=render.index('\nstatic void format',a)
render=render[:a]+'''static int readable(const char *p,const u64 *args){(void)args;return (uintptr_t)p>0x100000000UL;}
'''+render[b:]
original=(r/'out/p9/baseline/source/osfmk/kern/debug.c').read_text()
def body(name):
 start=original.index('\nvoid\n'+name+'(');i=original.index('{',start);n=1;j=i+1
 while n:n+=(original[j]=='{')-(original[j]=='}');j+=1
 return original[start:j]
prefix='''#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define DEBUGGER_INTERNAL_OPTIONS_MASK 0x200ULL
#define NULL ((void*)0)
typedef unsigned char uuid_t[16];
'''
main=r'''
const u64 gFont[]=GLYPH_FONT;
static unsigned char buffer[1080*1920*4+64];
static char line[4096];static unsigned calls;
static void ext_paniclog_panic_with_data(uuid_t uuid,void *data,uint32_t len){assert(uuid[0]==0x5a&&data&&len==7);}
static void panic_trap_to_debugger(const char *fmt,va_list *ap,unsigned reason,void *ctx,uint64_t options,void *data,unsigned long caller){
 (void)reason;(void)ctx;(void)options;(void)data;assert(caller);
 assert(sizeof(va_list)==sizeof(void*));
 memset(buffer,0,sizeof(buffer));memset(line,0,sizeof(line));
 struct ctx g={0};g.base=buffer+32;g.w=1080;g.h=1920;g.rb=4320;g.log=(u8*)line;
 format(&g,fmt,(const u64*)*ap);calls++;
 for(unsigned i=0;i<32;i++){assert(buffer[i]==0);assert(buffer[sizeof(buffer)-1-i]==0);}
}
'''
wrappers='\n'.join(body(x) for x in ['panic','panic_with_options','panic_with_data']).replace('\npanic(', '\nfixture_panic(').replace('\npanic_with_options(', '\nfixture_panic_with_options(').replace('\npanic_with_data(', '\nfixture_panic_with_data(')
fixture=prefix+render+'\n#include "Font.h"\n'+main+wrappers+r'''
int main(){
 fixture_panic("panic %s %u %llx", "provider",37U,0x123456789abcdef0ULL);assert(!strcmp(line,"panic provider 37 123456789abcdef0"));
 fixture_panic_with_options(7,(void*)0,0,"assert %.4s value=%x", "mapping",0xabcU);assert(!strcmp(line,"assert mapp value=abc"));
 uuid_t uuid={0x5a};char data[7]={0};fixture_panic_with_data(uuid,data,7,"data %s ptr=%p", "first",(void*)0x1234);assert(!strcmp(line,"data first ptr=0x1234"));
 fixture_panic("unsafe=%s",(const char*)0);assert(!strcmp(line,"unsafe=<0>"));
 assert(calls==4);puts("Actual formatter, first-panic wrapper va_list ABI, readable-pointer refusal and glyph bounds PASS (host only)");
}
'''
(o/'fixture.c').write_text(fixture);cmd=['xcrun','clang','-std=gnu11','-Wno-macro-redefined','-I',str(r/'drivers/p9'),str(o/'fixture.c'),'-o',str(o/'fixture')];subprocess.run(cmd,check=True);v=subprocess.run([str(o/'fixture')],capture_output=True,text=True);print(v.stdout,v.stderr,end='');assert v.returncode==0
(o/'result.json').write_text(json.dumps(dict(result='PASS',command=cmd,exit_code=v.returncode,real_source_wrappers=['panic','panic_with_options','panic_with_data'],renderer_sha256=hashlib.sha256((r/'drivers/p9/Renderer.inc').read_bytes()).hexdigest(),hardware_panic_proven=False,address_translation_is_host_fixture=True),indent=2)+'\n')
