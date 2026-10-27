from pathlib import Path
import shutil
r=Path('/project/UniXNU-cheeseburger');s=Path('/home/dinnerabc.guest/unixnu-p9/source');o=r/'out/p23/ios-16.7.16-20H392';assert (s/'.p23-stagec-overlay-marker').is_file()
for rel,name in [('P5Integration/RamPayload/CompressedPayload.h','CompressedPayload.h'),('P9Integration/Probe/P9Expected.h','Expected.h'),('DarwinBootPkg/Driver/P9Expected.h','Expected.h')]:shutil.copy2(o/'config'/name,s/rel)
print('R9 kernel/ADT/container pins updated; loader unchanged')
