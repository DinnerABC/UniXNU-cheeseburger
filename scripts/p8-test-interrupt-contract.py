#!/usr/bin/env python3
"""Targeted regression for the actual repeated IODT warning, no device probes."""
from pathlib import Path
import subprocess,json,hashlib,sys,struct
r=Path(__file__).resolve().parents[1];src=r/'.work/p4-unixnu/.cache/xnu-work/iokit/Kernel/IODeviceTreeSupport.cpp';s=src.read_text();a=s.index('static bool\nGetUInt32(');b=s.index('\nIOReturn\nIODTGetInterruptOptions',a);body=s[a:b]
o=r/'out/p8/interrupt-contract';o.mkdir(exist_ok=True)
prefix=r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <vector>
#include <map>
#include <string>
#include <stdexcept>
using UInt32=uint32_t; using IOItemCount=uint32_t;
#define LIBKERN_RETURNS_RETAINED
#define IODTSUPPORTDEBUG 0
struct OSObject {virtual ~OSObject(){} void release(){};};
struct OSSymbol:OSObject {std::string text; OSSymbol(const char*s):text(s){} static const OSSymbol* withCString(const char*s){return new OSSymbol(s);}};
struct OSData:OSObject {std::vector<uint8_t>b; OSData(const void*p,size_t n){if(n)b.assign((uint8_t*)p,(uint8_t*)p+n);} unsigned getLength(){return b.size();} void*getBytesNoCopy(){return b.data();} static OSData*withBytesNoCopy(void*p,size_t n){return new OSData(p,n);}};
struct IORegistryEntry {std::map<std::string,OSObject*>p;IORegistryEntry*parent=nullptr;
 OSObject*getProperty(const char*k){return p.count(k)?p[k]:nullptr;}
 OSObject*getProperty(const OSSymbol*k){return getProperty(k->text.c_str());}
 OSObject*copyProperty(const OSSymbol*k){return getProperty(k);}
 IORegistryEntry*getParentEntry(void*){return parent;}
};
#define OSDynamicCast(t,p) dynamic_cast<t*>(p)
static OSSymbol ic("#interrupt-cells"),ac("#address-cells"),ph("AAPL,phandle"),ip("interrupt-parent");
static const OSSymbol*gIODTInterruptCellKey=&ic,*gIODTAddressCellKey=&ac,*gIODTPHandleKey=&ph,*gIODTInterruptParentKey=&ip;
static void*gIODTPlane=nullptr;
static IORegistryEntry*FindPHandle(UInt32){return nullptr;}
static unsigned warnings;
static void kprintf(const char*,...){if(++warnings==8)throw std::runtime_error("bounded reproduction of repeated unexpected");}
'''
main=r'''
int main(){
 IORegistryEntry timer,bus;timer.parent=&bus;UInt32 irq=19;OSData*spec=nullptr;const OSSymbol*controller=nullptr;
 bool repeated=false;
 try { IODTMapOneInterrupt(&timer,&irq,0,&spec,&controller); }
 catch(const std::runtime_error&){repeated=true;}
 assert(repeated&&warnings==8);
 // Control: the unmodified mapper terminates with a proper controller graph.
 warnings=0;UInt32 one=1;bus.p["#interrupt-cells"]=new OSData(&one,4);bus.p["interrupt-controller"]=new OSData(nullptr,0);bus.p["AAPL,phandle"]=new OSData(&one,4);
 assert(IODTMapOneInterrupt(&timer,&irq,0,&spec,&controller)==1&&warnings==0);
 puts("Actual IODTMapOneInterrupt reproduced missing-parent warning loop; valid graph control terminates");
}
'''
(o/'test.cpp').write_text(prefix+body+main);cmd=['xcrun','clang++','-std=c++17',str(o/'test.cpp'),'-o',str(o/'test')];subprocess.run(cmd,check=True);v=subprocess.run([str(o/'test')],capture_output=True,text=True);print(v.stdout,end='');assert v.returncode==0
sys.path.insert(0,str(r/'tools/adt'));from adt import decode
t=decode((r/'out/p8/adt/kernel.macho.adt').read_bytes());bus=next(x for x in t['children'] if x['name']=='arm-io');timer=next(x for x in bus['children'] if x['name']=='timer');ufs=next(x for x in bus['children'] if x['name']=='ufshc@1da4000');assert 'interrupts' not in timer['properties'] and 'interrupts' not in ufs['properties'];assert timer['properties']['p8,timer-intid']==struct.pack('<I',19) and ufs['properties']['p8,spi-intid']==struct.pack('<I',297)
arm=(r/'drivers/p8/ArmCpu/ArmCpu.cpp').read_text();assert 'timer->getProperty ("p8,timer-intid")' in arm and 'rtclock_intr (0)' in arm and 'registerCPUInterruptController' in arm
(o/'result.json').write_text(json.dumps(dict(result='PASS',command=cmd,exit_code=v.returncode,source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),actual_mapper_body_tested=True,old_missing_parent_reproduced=True,fix='Raw PPI/INTID metadata consumed by actual CPU backend and polled UFS; do not advertise unimplemented generic IODT controller',timer_ppi=19,ufs_spi_intid=297,hardware_retested=False),indent=2)+'\n')
