// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540ProtocolAudit.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
// A pointer to the same named C++ struct has the same ABI when one protocol
// intentionally hides its fields. Never normalize pointer depth or scalar types.
bool NativeMetal540ProtocolTypesMatch(const char *a,const char *b){
    if(!a||!b)return false;
    if(!strcmp(a,b))return true;
    const char *pa=a,*pb=b;
    while(*pa=='r'||*pa=='n'||*pa=='N'||*pa=='o'||*pa=='O'||*pa=='R'||*pa=='V')++pa;
    while(*pb=='r'||*pb=='n'||*pb=='N'||*pb=='o'||*pb=='O'||*pb=='R'||*pb=='V')++pb;
    if(pa-a!=pb-b||strncmp(a,b,pa-a)||strncmp(pa,"^{",2)||strncmp(pb,"^{",2))return false;
    const char *ea=strchr(pa,'='),*eb=strchr(pb,'=');
    return ea&&eb&&ea-pa==eb-pb&&!strncmp(pa,pb,ea-pa)&&
        ((!strcmp(ea,"=}")&&eb[strlen(eb)-1]=='}')||(!strcmp(eb,"=}")&&ea[strlen(ea)-1]=='}'));
}
static void audit(id device,Protocol *protocol,Native540ProtocolAudit &result,std::vector<std::string> &seen,unsigned depth){
    if(!protocol||depth>8)return;
    unsigned n=0;objc_method_description *list=protocol_copyMethodDescriptionList(protocol,YES,YES,&n);
    for(unsigned i=0;i<n;++i){const auto &d=list[i];const char *name=sel_getName(d.name);
        bool duplicate=false;for(const auto &old:seen)if(old==name)duplicate=true;if(duplicate)continue;
        seen.emplace_back(name);++result.methods;Method m=class_getInstanceMethod(object_getClass(device),d.name);
        bool missing=!m,mismatch=false;
        if(!missing){NSMethodSignature *want=[NSMethodSignature signatureWithObjCTypes:d.types];
            NSMethodSignature *actual=[NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(m)];
            mismatch=!NativeMetal540ProtocolTypesMatch(want.methodReturnType,actual.methodReturnType)||want.numberOfArguments!=actual.numberOfArguments;
            for(NSUInteger k=0;k<want.numberOfArguments&&!mismatch;++k)mismatch=!NativeMetal540ProtocolTypesMatch([want getArgumentTypeAtIndex:k],[actual getArgumentTypeAtIndex:k]);}
        result.missing+=missing;result.mismatched+=mismatch;
        if((missing||mismatch)&&!result.first[0])snprintf(result.first,sizeof result.first,"%s:%s",missing?"missing":"type",name);
        if(missing||mismatch)fprintf(stderr,"DPM %s %s want=%s actual=%s\n",missing?"missing":"type",name,d.types,m?method_getTypeEncoding(m):"absent");
    }
    free(list);unsigned count=0;Protocol *__unsafe_unretained *parents=protocol_copyProtocolList(protocol,&count);
    for(unsigned i=0;i<count;++i)audit(device,parents[i],result,seen,depth+1);free(parents);
}
Native540ProtocolAudit NativeMetal540AuditDeviceProtocols(id device){
    Native540ProtocolAudit r{};if(!device){r.missing=1;snprintf(r.first,sizeof r.first,"missing device");return r;}
    Protocol *deviceProtocol=objc_getProtocol("MTLDevice");
    if(!deviceProtocol){r.missing=1;snprintf(r.first,sizeof r.first,"missing MTLDevice");return r;}
    std::vector<std::string> seen;audit(device,deviceProtocol,r,seen,0);
    Protocol *spi=objc_getProtocol("MTLDeviceSPI");if(!spi){++r.missing;snprintf(r.first,sizeof r.first,"missing MTLDeviceSPI");}
    else audit(device,spi,r,seen,0);return r;
}
