#!/usr/bin/env python3
"""Compile and run the actual locked DarwinBoot ADT parser on the generated tree."""
from pathlib import Path
import argparse,subprocess,hashlib,json
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('adt',type=Path);a=p.parse_args();s=r.parent/'upstream/DarwinBoot/DarwinBootPkg/Driver';code=(s/'DarwinBoot.c').read_text();start=code.index('STATIC\nBOOLEAN\nAdtNode (');end=code.index('\nSTATIC',start+1);body=code[start:end]
o=r/'out/p5/adt-parser';o.mkdir(parents=True,exist_ok=True)
prefix='''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define BOOLEAN bool
#define UINT8 uint8_t
#define UINT32 uint32_t
#define UINT64 uint64_t
#define UINTN size_t
#define CONST const
#define CHAR8 char
#define TRUE true
#define FALSE false
#define MAX_UINTN SIZE_MAX
#define ALIGN_VALUE(x,a) (((x)+(a)-1)&~((a)-1))
#define ScanMem8(b,n,v) memchr((b),(v),(n))
#define CompareMem memcmp
#define AsciiStrCmp strcmp
static uint32_t U32(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t U64(const uint8_t *p) {uint64_t v;memcpy(&v,p,8);return v;}
#include "WindowSize.h"
'''
main='''\nint main(int argc,char **argv) {
 if(argc!=2)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 2;
 fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);uint8_t *p=malloc(n);
 if(!p || fread(p,1,n,f)!=n)return 2;fclose(f);
 size_t at=0,ram=SIZE_MAX,trust=SIZE_MAX;uint64_t window=0;bool packed=false;
 bool ok=AdtNode(p,n,&at,0,3,&ram,&trust,&window,&packed) && at==n;
 printf("DarwinBoot AdtNode: %s bytes=%zu owned-request=0x%llx ramdisk-slot=%s\\n",ok?"PASS":"FAIL",at,(unsigned long long)window,ram==SIZE_MAX?"absent":"present");
 free(p);return ok?0:1;
}
'''
(o/'validate.c').write_text(prefix+body+main)
cmd=['xcrun','clang','-std=c11','-O2','-I',str(s),str(o/'validate.c'),'-o',str(o/'validate')];subprocess.run(cmd,check=True)
v=subprocess.run([str(o/'validate'),str(a.adt.resolve())],capture_output=True,text=True);print(v.stdout,end='');(o/'validation.json').write_text(json.dumps(dict(command=cmd,source_sha256=hashlib.sha256((s/'DarwinBoot.c').read_bytes()).hexdigest(),adt_sha256=hashlib.sha256(a.adt.read_bytes()).hexdigest(),exit_code=v.returncode,output=v.stdout),indent=2)+'\n');raise SystemExit(v.returncode)
