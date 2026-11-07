// SPDX-License-Identifier: MIT
// Library ownership/index/lazy compiler semantics follow pinned sagit QcomMTL.
#include "NativeMetal540DesktopLibrary.h"
#include "LibraryArchive.h"
#include "FunctionConstants.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cstring>
#include <map>

static std::string digest(const void *bytes,size_t size) {
    unsigned char hash[32];CC_SHA256(bytes,CC_LONG(size),hash);
    const char *hex="0123456789abcdef";std::string out;
    for(auto b:hash){out+=hex[b>>4];out+=hex[b&15];}return out;
}
std::shared_ptr<NativeMetal540DesktopLibrary> NativeMetal540DesktopLibrary::load(
    const void *bytes,size_t size,Compile compile,uint64_t registry,std::string &error,CBMetalInspectConstants inspector) {
    error.clear();
    if(!bytes||!size||size>GPU::Compiler::LibraryArchive::MaxBytes||!compile){error="Bounded original library and compiler required";return {};}
    auto out=std::shared_ptr<NativeMetal540DesktopLibrary>(new NativeMetal540DesktopLibrary);
    out->bytes_.assign(static_cast<const uint8_t *>(bytes),static_cast<const uint8_t *>(bytes)+size);
    std::vector<GPU::Compiler::LibraryArchive::Function> functions;
    if(!GPU::Compiler::LibraryArchive::parse(out->bytes_.data(),size,functions,error))return {};
    std::map<std::string,std::array<uint8_t,32>> seen;
    for(const auto &f:functions){
        unsigned char hash[32];CC_SHA256(out->bytes_.data()+f.offset,CC_LONG(f.size),hash);
        if(memcmp(hash,f.hash.data(),32)){error="Original AIR module hash mismatch";return {};}
        // Authentication covers every module, including unsupported stages.
        if(f.type>2||f.specialized)continue;
        auto match=seen.emplace(f.name,f.hash);
        if(!match.second){if(match.first->second!=f.hash){error="Ambiguous original function name";return {};}continue;}
        out->entries_.push_back({f.name,f.offset,f.size,f.type==2?0u:f.type==0?1u:2u});out->names_.push_back(f.name);
    }
    if(out->entries_.empty()){error="Library has no supported portable AIR stages";return {};}
    out->identity_=digest(out->bytes_.data(),size);out->compile_=compile;out->registry_=registry;out->inspector_=inspector;return out;
}
NativeMetal540DesktopLibrary::Image NativeMetal540DesktopLibrary::function(const std::string &name,const std::vector<GPU::Compiler::FunctionConstantValue> &input,CBMetalInspectConstants inspector,std::string &error) {
    if(!inspector){error="Original AIR declaration inspector required";return {};}
    auto at=std::find_if(entries_.begin(),entries_.end(),[&](const Entry &e){return e.name==name;});
    if(at==entries_.end()){error="Unknown or unsupported original AIR entry";return {};}
    std::vector<CBMetalConstantDeclaration> bridge(256);unsigned count=unsigned(bridge.size());char diagnostic[4096]{};
    if(!inspector(bytes_.data()+at->offset,at->bytes,name.c_str(),bridge.data(),&count,diagnostic,sizeof diagnostic)||count>bridge.size()){
        error=diagnostic[0]?diagnostic:"Original AIR declaration inspection failed";return {};}
    std::vector<GPU::Compiler::FunctionConstant> declared;std::map<uint32_t,bool> indexes;std::map<std::string,bool> names;
    for(unsigned i=0;i<count;++i){const auto &d=bridge[i];size_t n=strnlen(d.name,sizeof d.name);
        if(!n||n==sizeof d.name||d.index>65535||!GPU::Compiler::constantBytes(d.type)||d.required>1||
            !indexes.emplace(d.index,true).second||!names.emplace(std::string(d.name,n),true).second){error="Invalid original constant declaration";return {};}
        declared.push_back({std::string(d.name,n),d.index,d.type,bool(d.required)});
    }
    std::vector<GPU::Compiler::FunctionConstantValue> effective;
    if(!GPU::Compiler::resolveFunctionConstants(declared,input,effective,error))return {};
    std::vector<QGPUAIRConstant> values;
    for(const auto &v:effective){QGPUAIRConstant item{};item.index=v.index;item.type=v.type;memcpy(item.bytes,v.bytes.data(),sizeof item.bytes);values.push_back(item);}
    return function(name,values,error);
}
NativeMetal540DesktopLibrary::Image NativeMetal540DesktopLibrary::function(const std::string &name,std::string &error) {
    return function(name,std::vector<QGPUAIRConstant>{},error);
}
NativeMetal540DesktopLibrary::Image NativeMetal540DesktopLibrary::function(const std::string &name,const std::vector<QGPUAIRConstant> &input,std::string &error) {
    std::lock_guard<std::mutex> guard(lock_);error.clear();
    if(input.size()>GPU::Compiler::MaxConstantValues){error="Too many function constants";return {};}
    auto constants=input;std::sort(constants.begin(),constants.end(),[](const auto &a,const auto &b){return a.index<b.index;});
    std::string key=name;key.push_back('\0');
    for(size_t i=0;i<constants.size();++i){auto &v=constants[i];unsigned bytes=GPU::Compiler::constantBytes(v.type);
        if(!bytes||v.index>65535||(i&&constants[i-1].index==v.index)){error="Invalid or duplicate function constants";return {};}
        if(GPU::Compiler::constantShape(v.type).boolean)for(unsigned j=0;j<bytes;++j)v.bytes[j]=v.bytes[j]!=0;
        memset(v.bytes+bytes,0,sizeof(v.bytes)-bytes);
        key.append(reinterpret_cast<const char *>(&v.index),sizeof v.index);key.append(reinterpret_cast<const char *>(&v.type),sizeof v.type);key.append(reinterpret_cast<const char *>(v.bytes),bytes);
    }
    auto at=std::find_if(entries_.begin(),entries_.end(),[&](const Entry &entry){return entry.name==name;});
    if(at==entries_.end()){error="Unknown or unsupported original AIR entry";return {};}
    for(auto &cached:cache_)if(cached.name==key)if(auto image=cached.image.lock())return image;
    cache_.erase(std::remove_if(cache_.begin(),cache_.end(),[](const Cached &v){return v.image.expired();}),cache_.end());
    if(cache_.size()>=256){error="Resident shader cache capacity reached";return {};}
    auto image=std::make_shared<QGPUProgramImage>();QGPUAIRNIRTiming timing{};char diagnostic[4096]{};
    if(!compile_(bytes_.data()+at->offset,at->bytes,name.c_str(),constants.empty()?nullptr:constants.data(),unsigned(constants.size()),registry_,image.get(),&timing,diagnostic,sizeof diagnostic)) {
        error=diagnostic[0]?diagnostic:"Original AIR compiler failed";return {};
    }
    if(!GPU::JIT::valid(*image,registry_)||image->info.stage!=at->stage){error="Compiler image/registry/stage validation failed";return {};}
    cache_.push_back({key,image});return image;
}
