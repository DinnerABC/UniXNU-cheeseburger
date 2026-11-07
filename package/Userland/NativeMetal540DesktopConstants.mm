// SPDX-License-Identifier: MIT
// Snapshot semantics follow pinned sagit QcomMTL::snapshotConstants; target
// encodings are verified against the actual runtime before any invocation.
#include "NativeMetal540DesktopConstants.h"
#include "FunctionConstants.h"
#import <objc/runtime.h>
#include <cstring>
#include <algorithm>
@interface MTLFunctionConstantValues (CBMetalSnapshot)
- (NSArray *)newIndexedConstantArray;
- (NSArray *)newNamedConstantArray;
@end
@protocol CBMetalIndexedConstant
- (NSUInteger)dataType;
- (NSUInteger)index;
- (void *)data;
@end
@protocol CBMetalNamedConstant <CBMetalIndexedConstant>
- (NSString *)name;
@end
bool CBMetalSnapshotConstants(MTLFunctionConstantValues *values,std::vector<GPU::Compiler::FunctionConstantValue> &out,NSError **error) {
    out.clear();if(error)*error=nil;if(!values)return true;
    auto fail=[&](NSString *text){out.clear();if(error)*error=[NSError errorWithDomain:MTLLibraryErrorDomain code:MTLLibraryErrorCompileFailure userInfo:@{NSLocalizedDescriptionKey:text}];return false;};
    if(![values isKindOfClass:MTLFunctionConstantValues.class])return fail(@"Actual Metal function constants required");
    const char *methods[][3]={{"MTLFunctionConstantValuesInternal","newIndexedConstantArray","@16@0:8"},
        {"MTLFunctionConstantValuesInternal","newNamedConstantArray","@16@0:8"},{"MTLIndexedConstantValue","dataType","Q16@0:8"},
        {"MTLIndexedConstantValue","index","Q16@0:8"},{"MTLIndexedConstantValue","data","^v16@0:8"},
        {"MTLNamedConstantValue","dataType","Q16@0:8"},{"MTLNamedConstantValue","name","@16@0:8"},
        {"MTLNamedConstantValue","data","^v16@0:8"}};
    for(const auto &entry:methods){Class cls=objc_lookUpClass(entry[0]);Method method=cls?class_getInstanceMethod(cls,sel_registerName(entry[1])):nullptr;
        if(!method||strcmp(method_getTypeEncoding(method),entry[2]))return fail(@"Target function constant encoding mismatch");}
    MTLFunctionConstantValues *copy=[values copy];NSArray *indexed=[copy newIndexedConstantArray],*named=[copy newNamedConstantArray];
    // Indexed and named values both come from the runtime's own snapshot
    // objects; named entries resolve through the original AIR declarations.
    if(indexed.count+named.count>GPU::Compiler::MaxConstantValues)return fail(@"Too many function constants");
    Class cls=objc_lookUpClass("MTLIndexedConstantValue"),namedClass=objc_lookUpClass("MTLNamedConstantValue");
    for(id object in indexed){if(![object isKindOfClass:cls])return fail(@"Invalid original constant object");
        auto source=(id<CBMetalIndexedConstant>)object;NSUInteger type=source.dataType,index=source.index;unsigned bytes=type<=UINT32_MAX?GPU::Compiler::constantBytes(uint32_t(type)):0;
        if(!bytes||index>65535||!source.data)return fail(@"Invalid original constant type/index/data");
        GPU::Compiler::FunctionConstantValue entry;entry.index=uint32_t(index);entry.type=uint32_t(type);memcpy(entry.bytes.data(),source.data,bytes);
        if(GPU::Compiler::constantShape(entry.type).boolean)for(unsigned i=0;i<bytes;++i)entry.bytes[i]=entry.bytes[i]!=0;
        out.push_back(entry);
    }
    for(id object in named){if(![object isKindOfClass:namedClass])return fail(@"Invalid original named constant object");
        auto value=(id<CBMetalNamedConstant>)object;NSString *name=value.name;NSUInteger type=value.dataType;unsigned bytes=type<=UINT32_MAX?GPU::Compiler::constantBytes(uint32_t(type)):0;
        if(!name.length||!name.UTF8String||!bytes||!value.data)return fail(@"Invalid original named constant type/name/data");
        GPU::Compiler::FunctionConstantValue entry;entry.name=name.UTF8String;entry.index=UINT32_MAX;entry.type=uint32_t(type);memcpy(entry.bytes.data(),value.data,bytes);
        if(GPU::Compiler::constantShape(entry.type).boolean)for(unsigned i=0;i<bytes;++i)entry.bytes[i]=entry.bytes[i]!=0;
        out.push_back(entry);
    }
    return true;
}
