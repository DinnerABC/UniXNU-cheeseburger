#pragma once
#include <stdint.h>
class OSDictionary;
class OSObject;
extern "C" uint64_t p21_match_begin(OSDictionary *,const char *);
extern "C" void p21_match_end(uint64_t,int,OSObject *);
extern "C" void p21_match_next(OSObject *,OSObject *,int);
extern "C" unsigned p21_match_dump(char *,unsigned);
