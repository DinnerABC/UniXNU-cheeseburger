// SPDX-License-Identifier: MIT
#pragma once
#include <GPUJITProtocol.h>
#include "AIRNIR.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Own and authenticate the original system library before lazy compilation.
// This loader is not a Metal device and does not publish an incomplete protocol.
class NativeMetal540DesktopLibrary {
public:
    using Compile=int (*)(const void *,size_t,const char *,const QGPUAIRConstant *,unsigned,
                         uint64_t,QGPUProgramImage *,QGPUAIRNIRTiming *,char *,unsigned);
    using Image=std::shared_ptr<const QGPUProgramImage>;
    static std::shared_ptr<NativeMetal540DesktopLibrary> load(const void *bytes,size_t size,
        Compile compile,uint64_t registry,std::string &error);
    Image function(const std::string &name,std::string &error);
    const std::vector<std::string> &names() const {return names_;}
    const std::string &identity() const {return identity_;}
private:
    struct Entry {std::string name;size_t offset=0,bytes=0;unsigned stage=0;};
    struct Cached {std::string name;std::weak_ptr<const QGPUProgramImage> image;};
    std::vector<uint8_t> bytes_;
    std::vector<Entry> entries_;
    std::vector<std::string> names_;
    std::vector<Cached> cache_;
    std::string identity_;
    Compile compile_=nullptr;uint64_t registry_=0;
    std::mutex lock_;
};
