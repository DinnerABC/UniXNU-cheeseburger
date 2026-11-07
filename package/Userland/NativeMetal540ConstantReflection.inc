// SPDX-License-Identifier: MIT
#include "NativeMetal540ConstantReflection.h"
#include <cstring>
#include <vector>
#include <string>
// Built in the same compiler translation unit as the pinned inspector. It
// exposes copied metadata only, never LLVM/NIR/C++ objects across the dylib.
extern "C" int cbmetal_inspect_air_constants(const void *bytes,size_t size,const char *entry,
    CBMetalConstantDeclaration *out,unsigned *count,char *error,unsigned capacity) {
    if(!bytes||!size||size>16u*1024*1024||!entry||strlen(entry)>=96||!count||!error||!capacity)return 0;
    const unsigned limit=*count;*count=0;error[0]=0;
    if(limit>256||(limit&&!out)){snprintf(error,capacity,"Invalid constant output capacity");return 0;}
    std::vector<GPU::Compiler::FunctionConstant> declarations;unsigned stage=0;std::string diagnostic;
    if(!GPU::Compiler::inspectAIRConstants(bytes,size,entry,declarations,stage,diagnostic)) {
        snprintf(error,capacity,"%s",diagnostic.c_str());return 0;
    }
    if(declarations.size()>limit){snprintf(error,capacity,"Constant declaration capacity exceeded");return 0;}
    for(const auto &d:declarations)if(d.name.empty()||d.name.size()>=sizeof(out[0].name)||d.index>65535||!GPU::Compiler::constantBytes(d.type)) {
        snprintf(error,capacity,"Invalid original AIR constant declaration");return 0;
    }
    for(size_t i=0;i<declarations.size();++i){const auto &d=declarations[i];out[i]={d.index,d.type,unsigned(d.required),{}};memcpy(out[i].name,d.name.data(),d.name.size());}
    *count=unsigned(declarations.size());return 1;
}
