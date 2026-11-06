// SPDX-License-Identifier: MIT
#include "NativeMetal540RuntimeABI.h"
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>
unsigned NativeMetal540CaptureABI(char (*rows)[64],unsigned capacity){
 if(!rows||!capacity)return 0;
 unsigned n=0;Class base=objc_getClass("_MTLDevice");
 snprintf(rows[n++],64,"MA class=%s super=%s size=%llu",base?class_getName(base):"absent",
  base&&class_getSuperclass(base)?class_getName(class_getSuperclass(base)):"absent",
  (unsigned long long)(base?class_getInstanceSize(base):0));
 const char *selectors[]={"init","dealloc","initWithAcceleratorPort:","initLimits","initFeatureQueries",
  "initWorkarounds","initGPUFamilySupport","limits","maxBufferLength","maxThreadsPerThreadgroup",
  "maxThreadgroupMemoryLength","supportsFamily:"};
 for(const char *name:selectors){
  if(n>=capacity)break;Method method=base?class_getInstanceMethod(base,sel_registerName(name)):nullptr;
  const char *type=method?method_getTypeEncoding(method):"absent";
  if(!strcmp(name,"limits")&&strlen(type)>40){
   unsigned length=(unsigned)strlen(type);
   if(n<capacity)snprintf(rows[n++],64,"MA limits-type-len=%u",length);
   for(unsigned at=0;at<length&&n<capacity;at+=48)
    snprintf(rows[n++],64,"MA LT%u=%.48s",at,type+at);
  }else snprintf(rows[n++],64,"MA %s=%s",name,type);
 }
 Protocol *spi=objc_getProtocol("MTLDeviceSPI");
 if(n<capacity)snprintf(rows[n++],64,"MA protocol=MTLDeviceSPI present=%u",spi?1u:0u);
 return n;
}
