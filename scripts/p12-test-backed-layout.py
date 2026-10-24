from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parents[1];o=r/'out/p12/backed-layout';o.mkdir(exist_ok=True);source=r/'.work/p4-unixnu/drivers/NativeUfs'
code='''#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>
using IOReturn=uint32_t;constexpr IOReturn kIOReturnSuccess=0;constexpr unsigned kDataMax=131072;
#include "RootDiagnostics.h"
static UfsRootDiagnostics gRootDiagnostic;
static std::vector<uint8_t> gpt;static const char *image=nullptr;static uint64_t userdata=2441472;
static IOReturn hostRead4k(uint8_t lun,uint64_t lba,uint32_t n,void *out){
 if(lun)return 1;
 if(lba*4096<gpt.size()&&n*4096<=gpt.size()-lba*4096){memcpy(out,gpt.data()+lba*4096,n*4096);return 0;}
 if(image&&lba>=userdata){std::ifstream f(image,std::ios::binary);f.seekg((lba-userdata)*4096);f.read((char*)out,n*4096);return f.gcount()==n*4096?0:1;}
 return 1;
}
#include "RootLayout.h"
int main(int argc,char **argv){if(argc<2)return 2;std::ifstream f(argv[1],std::ios::binary);gpt.assign(std::istreambuf_iterator<char>(f),{});if(argc==3)image=argv[2];RootLayout layout;const char *why="";bool ok=findRootLayout(&layout,&why);
 if(!image){if(ok||strcmp(why,"userdata first block read")||layout.userdataFirst!=2441472||layout.userdataLast!=14145279)return 1;puts("PASS actual P3 GPT geometry/CRC; manifest NOT OBSERVED, read unavailable (not status=0 success)");return 0;}
 if(!ok||layout.manifestStatus||layout.windows!=2)return 1;
 printf("PASS actual GPT + actual root image SHNGROOT; first=%llu last=%llu windows=%u\\n",(unsigned long long)layout.userdataFirst,(unsigned long long)layout.userdataLast,layout.windows);return 0;}
'''
(o/'backed.cpp').write_text(code);cmd=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(source),'-I/opt/homebrew/opt/openssl@3/include',str(o/'backed.cpp'),'-L/opt/homebrew/opt/openssl@3/lib','-lcrypto','-o',str(o/'backed')];subprocess.run(cmd,check=True)
args=[str(o/'backed'),str(r/'backups/p3/gpt-20261022-023658/sda-primary.bin')]
import sys
if len(sys.argv)>1:args.append(str(Path(sys.argv[1]).resolve()))
subprocess.run(args,check=True)
