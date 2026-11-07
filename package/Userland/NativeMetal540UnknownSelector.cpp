// SPDX-License-Identifier: MIT
#include "NativeMetal540UnknownSelector.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <mutex>
static std::mutex lock;
static std::vector<std::string> names;
static unsigned count;
void CBMetalRecordUnknownSelectorName(const char *text) {
    if(!text||!*text)return;
    std::lock_guard<std::mutex> guard(lock);
    bool duplicate=false;
    for(const auto &known:names)if(known==text)duplicate=true;
    if(!duplicate&&names.size()<32)names.push_back(text);
    ++count;
    if(int fd=open("/private/var/tmp/p21-plugin.txt",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0644);fd>=0){
        char line[160];int n=snprintf(line,sizeof line,"CBUNKNOWN %s\n",text);
        write(fd,line,(size_t)n);close(fd);
    }
}
unsigned CBMetalUnknownSelectorCount(void){std::lock_guard<std::mutex> guard(lock);return count;}
std::vector<std::string> CBMetalUnknownSelectorList(void){
    std::lock_guard<std::mutex> guard(lock);return names;
}
