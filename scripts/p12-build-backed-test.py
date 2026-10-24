from pathlib import Path
import subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p12/backed-root';o.mkdir(exist_ok=True);v=r/'.work/p4-unixnu/kits/VerifiedRoot';inc=['-I'+str(v/'include'),'-I/opt/homebrew/opt/openssl@3/include']
for p in [v/'src/verified_root.c',v/'tests/openssl_provider.c']:
 subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',*inc,'-c',str(p),'-o',str(o/(p.stem+'.o'))],check=True)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',*inc,'-I'+str(r/'drivers/p12/NativeUfs'),str(r/'tools/p12/BackedRoot.cpp'),str(o/'verified_root.o'),str(o/'openssl_provider.o'),'-L/opt/homebrew/opt/openssl@3/lib','-lcrypto','-o',str(o/'backed-root')],check=True)
