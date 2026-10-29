from pathlib import Path
import shutil
r=Path('/project/UniXNU-cheeseburger');s=Path('/home/dinnerabc.guest/unixnu-up1-clean/source');o=r/'out/p21/up1-clean-rebuild';assert (s/'.up1-clean-overlay-marker').is_file()
for rel,name in [('P5Integration/RamPayload/CompressedPayload.h','CompressedPayload.h'),('P9Integration/Probe/P9Expected.h','Expected.h'),('DarwinBootPkg/Driver/P9Expected.h','Expected.h')]:shutil.copy2(o/'config'/name,s/rel)
print('R9 kernel/ADT/container pins updated; loader unchanged')
