from pathlib import Path
r=Path(__file__).resolve().parents[1];p=r/'drivers/p12/NativeUfs/NativeUfs.cpp';s=p.read_text();needle='static NativeUfsBlock *gPendingDevice[4];'
helper='''static bool p12RootMatchingEquals(const char *uuid)
{
 IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 if(!chosen)return false;
 OSData *data=OSDynamicCast(OSData,chosen->getProperty("root-matching"));
 OSObject *object=nullptr;bool valid=false;
 if(data&&data->getLength()>0&&data->getLength()<=4096&&
    static_cast<const char*>(data->getBytesNoCopy())[data->getLength()-1]==0){
  object=OSUnserializeXML(static_cast<const char*>(data->getBytesNoCopy()));
  OSDictionary *dict=OSDynamicCast(OSDictionary,object);
  OSString *provider=dict?OSDynamicCast(OSString,dict->getObject("IOProviderClass")):nullptr;
  OSDictionary *properties=dict?OSDynamicCast(OSDictionary,dict->getObject("IOPropertyMatch")):nullptr;
  OSString *wanted=properties?OSDynamicCast(OSString,properties->getObject("UUID")):nullptr;
  valid=provider&&provider->isEqualTo("IOMedia")&&wanted&&wanted->isEqualTo(uuid);
 }
 OSSafeReleaseNULL(object);chosen->release();return valid;
}
'''
assert 'p12RootMatchingEquals' not in s;s=s.replace(needle,helper+needle).replace('#include <IOKit/IOBSD.h>','#include <IOKit/IOBSD.h>\n#include <IOKit/IODeviceTreeSupport.h>\n#include <libkern/c++/OSUnserialize.h>')
s=s.replace('if (!gPendingDevice[i]->signedVolumeUuid (gPendingUuid[i])) return false;','if (!gPendingDevice[i]->signedVolumeUuid (gPendingUuid[i])) return false;\n        if (!strcmp(gPendingDevice[i]->windowLabel(),"OS")&&!p12RootMatchingEquals(gPendingUuid[i])) return false;')
p.write_text(s)
