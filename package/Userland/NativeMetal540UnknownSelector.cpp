// SPDX-License-Identifier: MIT
#include "NativeMetal540UnknownSelector.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include <mutex>
static std::mutex lock;
static std::vector<std::string> names;
static unsigned count;
static CBMetalReportSend reportSend;
static void *reportConnection;
void CBMetalSetReportSend(CBMetalReportSend send,void *connection){reportSend=send;reportConnection=connection;}
// The kernel SetMetadata channel persists only prefixed rows; "MA " lands in
// the per-handle ABI array the log decoder prints. The sender is injected by
// the plugin so this TU stays pure C++.
static void reportRow(const char *text) {
    if(!reportSend||!reportConnection||!text)return;
    reportSend(text);
}
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
    char row[80];snprintf(row,sizeof row,"MA UNK %.48s",text);
    reportRow(row);
}
unsigned CBMetalUnknownSelectorCount(void){std::lock_guard<std::mutex> guard(lock);return count;}
std::vector<std::string> CBMetalUnknownSelectorList(void){
    std::lock_guard<std::mutex> guard(lock);return names;
}
