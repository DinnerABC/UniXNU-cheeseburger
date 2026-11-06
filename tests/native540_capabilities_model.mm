// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540Capabilities.h"
#import <objc/message.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace NativeMetal540Capabilities;
#ifndef NATIVE540_CAPABILITIES_HOST_MODEL
#error Host mock only; no real macOS _MTLDevice
#endif
static size_t initialized=0;
static BOOL inheritedUnsafe(id self, SEL command) {
    (void)self;(void)command;std::abort(); // models unsafe feature-object dereference
}
static id mockInit(id self,SEL command) {
    (void)command;
    for(size_t i=0;i<count();++i) {
        BOOL value=((BOOL(*)(id,SEL))objc_msgSend)(self,sel_registerName(entries()[i].selector));
        assert(value==(entries()[i].supported?YES:NO));++initialized;
    }
    return self;
}
static size_t ownQueries(Class cls) {
    unsigned int n=0;Method *methods=class_copyMethodList(cls,&n);size_t result=0;
    for(unsigned int i=0;i<n;++i)for(size_t j=0;j<count();++j)
        result+=sel_isEqual(method_getName(methods[i]),sel_registerName(entries()[j].selector));
    std::free(methods);return result;
}
int main(int argc,char **argv) { @autoreleasepool {
    const char *scenario=argc>1?argv[1]:"ok";const uint32_t flags=minimumEvidenceFlags();
    const bool inherited=!std::strcmp(scenario,"inherited");
    if(inherited) {
        // Registered unrelated class proves own-method preflight rejects fallback.
        assert(class_addMethod([NSObject class],sel_registerName(entries()[194].selector),(IMP)inheritedUnsafe,"B16@0:8"));
    }
    Class base=objc_allocateClassPair([NSObject class],"Native540CapabilitiesMockBase",0);assert(base);
    size_t desired=!std::strcmp(scenario,"size")?528:536;
    assert(class_addIvar(base,"_padding",desired-class_getInstanceSize([NSObject class]),0,"[528c]"));
    for(size_t i=0;i<count();++i) {
        if(i+1==count()&&(!std::strcmp(scenario,"missing")||inherited))continue;
        const char *type=i+1==count()&&!std::strcmp(scenario,"type")?"c16@0:8":"B16@0:8";
        assert(class_addMethod(base,sel_registerName(entries()[i].selector),(IMP)inheritedUnsafe,type));
    }
    assert(class_addMethod(base,@selector(init),(IMP)mockInit,"@16@0:8"));objc_registerClassPair(base);
    Class sub=objc_allocateClassPair(base,"NativeMetal540CapabilitiesMockDevice",0);assert(sub);
    if(!std::strcmp(scenario,"subivar"))assert(class_addIvar(sub,"_extra",8,3,"Q"));
    if(!std::strcmp(scenario,"conflict"))assert(class_addMethod(sub,sel_registerName(entries()[194].selector),(IMP)inheritedUnsafe,"B16@0:8"));
    if(!std::strcmp(scenario,"registered"))objc_registerClassPair(sub);
    Report report;
    if(!std::strcmp(scenario,"evidence")) {
        for(unsigned bit=0;bit<3;++bit) {
            assert(!NativeMetal540InstallQueries(sub,base,Evidence(flags&~(1u<<bit)),report));
            assert(report.status==Status::MissingEvidence&&report.missingEvidenceFlags==(1u<<bit));
            assert(!report.preflightComplete&&!report.installed&&!ownQueries(sub));
        }
        assert(!NativeMetal540InstallQueries(sub,base,Evidence(flags|0x80000000),report));
        assert(report.status==Status::InvalidEvidence&&!ownQueries(sub));
    } else if(!std::strcmp(scenario,"invalid")) {
        assert(!NativeMetal540InstallQueries(base,base,Evidence(flags),report));
        assert(report.status==Status::InvalidClass&&!report.installed);
        assert(!NativeMetal540InstallQueries(sub,[NSObject class],Evidence(flags),report));
        assert(report.status==Status::InvalidClass&&!ownQueries(sub));
    } else {
        bool good=!std::strcmp(scenario,"ok");
        assert(NativeMetal540InstallQueries(sub,base,Evidence(flags),report)==good);
        if(!good) {
            assert(!report.preflightComplete&&!report.installed&&!report.discardSubclass);
            assert(ownQueries(sub)==(!std::strcmp(scenario,"conflict")?1u:0u));
            if(!std::strcmp(scenario,"missing")||inherited)assert(report.status==Status::BaseMethodMissing&&report.checked==194);
            if(!std::strcmp(scenario,"type"))assert(report.status==Status::TypeMismatch&&report.checked==194);
            if(!std::strcmp(scenario,"conflict"))assert(report.status==Status::SubclassMethodExists&&report.checked==194);
            if(!std::strcmp(scenario,"size")||!std::strcmp(scenario,"subivar"))assert(report.status==Status::BaseSizeMismatch);
            if(!std::strcmp(scenario,"registered"))assert(report.status==Status::InvalidClass);
        } else {
            assert(report.status==Status::Ok&&report.checked==195&&report.installed==195&&report.supported==1&&report.preflightComplete&&!report.discardSubclass);
            assert(ownQueries(sub)==195&&ownQueries(base)==195);objc_registerClassPair(sub);
            id object=[[sub alloc] init];assert(object&&initialized==195);
            for(size_t i=0;i<count();++i) {
                const Entry &entry=entries()[i];BOOL value=NO;const char *reason=nullptr;
                assert(answer(entry.selector,value,reason)==Status::Ok&&reason&&reason[0]);
                assert(value==(entry.supported?YES:NO));
                Method method=class_getInstanceMethod(sub,sel_registerName(entry.selector));
                assert(!std::strcmp(method_getTypeEncoding(method),"B16@0:8"));
                assert(method_getImplementation(method)!=method_getImplementation(class_getInstanceMethod(base,sel_registerName(entry.selector))));
                for(size_t j=0;j<i;++j)assert(std::strcmp(entry.selector,entries()[j].selector));
            }
            BOOL untouched=YES;const char *reason="untouched";
            for(const char *unknown:{"supportsUnknownFeature", "", (const char*)nullptr}) {
                assert(answer(unknown,untouched,reason)==Status::UnknownSelector);
                assert(untouched==YES&&!std::strcmp(reason,"untouched"));
            }
            assert(![object respondsToSelector:sel_registerName("supportsUnknownFeature")]);
            assert(!NativeMetal540InstallQueries(sub,base,Evidence(flags),report));
            assert(report.status==Status::InvalidClass&&!report.installed);
        }
    }
    std::printf("PASS %s: host Objective-C policy/runtime only, no device/GPU claim\n",scenario);
} }
