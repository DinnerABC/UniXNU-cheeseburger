#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,hashlib
r=Path(__file__).resolve().parents[1];s=r.parent/'upstream/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c';code=s.read_text();start=code.index('STATIC\nEFI_STATUS\nParseKernel (');end=code.index('\nSTATIC',start+1);body=code[start:end];types=code[code.index('typedef struct {'):code.index('STATIC EFI_HANDLE')]
prefix='''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define CONST const
#define VOID void
#define UINT8 uint8_t
#define UINT32 uint32_t
#define UINT64 uint64_t
#define UINTN size_t
#define BOOLEAN bool
#define EFI_STATUS int
#define TRUE true
#define FALSE false
#define EFI_UNSUPPORTED 3
#define EFI_LOAD_ERROR 1
#define EFI_SUCCESS 0
#define MAX_UINT64 UINT64_MAX
#define MAX_SEGMENTS 64
#define WINDOW_SIZE 0x40000000ULL
#define SIZE_2MB 0x200000ULL
#define SIZE_256MB 0x10000000ULL
#define EFI_PAGE_MASK 4095ULL
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ZeroMem(p,n) memset(p,0,n)
static uint32_t U32(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t U64(const void *p){uint64_t v;memcpy(&v,p,8);return v;}
'''
# Take the actual bound from pinned source, not a permissive local replacement.
import re
for name in ['MAX_SEGMENTS','WINDOW_SIZE']:
 match=re.search(r'^#define\s+'+name+r'\s+(.+)$',code,re.M);assert match
 prefix=re.sub(r'^#define '+name+r' .+$','#define '+name+' '+match[1],prefix,flags=re.M)
main='''
int main(int argc,char **argv){
 if(argc!=2)return 2;FILE *f=fopen(argv[1],"rb");if(!f)return 2;
 fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);uint8_t *p=malloc(n);if(!p||fread(p,1,n,f)!=n)return 2;fclose(f);
 KERNEL k;int status=ParseKernel(p,n,&k);printf("Actual DarwinBoot ParseKernel status=%d segments=%zu entry=0x%llx span=0x%llx\\n",status,k.Count,(unsigned long long)k.Entry,(unsigned long long)(k.End-k.Base));
 if(status)return 1;
 p[0]^=1;if(ParseKernel(p,n,&k)==0)return 1;
 free(p);puts("Corrupted Mach-O refused");return 0;
}
'''
o=r/'out/p12/loader-parser';o.mkdir(parents=True,exist_ok=True);(o/'validate.c').write_text(prefix+types+body+main);cmd=['xcrun','clang','-std=c11','-O2',str(o/'validate.c'),'-o',str(o/'validate')];subprocess.run(cmd,check=True)
v=subprocess.run([str(o/'validate'),str(r/'out/p12/kernel/kernel.macho')],capture_output=True,text=True);print(v.stdout,end='');(o/'validation.json').write_text(json.dumps(dict(command=cmd,exit_code=v.returncode,output=v.stdout,parser_source_sha256=hashlib.sha256(s.read_bytes()).hexdigest(),scope='Actual locked parser on host; not hardware load evidence'),indent=2)+'\n');raise SystemExit(v.returncode)
