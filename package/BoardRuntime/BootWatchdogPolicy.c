// SPDX-License-Identifier: MIT
// Derived from UniXNU-sheng/package/BootWatchdogPolicy.c.
// development-only watchdogd job isolation; no hardware watchdog support implied.
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <pexpert/pexpert.h>
volatile int cheeseburger_watchdog_policy_status=-1;
volatile int cheeseburger_watchdog_policy_requested;
volatile int cheeseburger_watchdog_policy_applied;
#include "BootUserlandPolicy.h"
#include "BoardBootArgs.h"
volatile int cheeseburger_upstream_policy_requested,cheeseburger_upstream_policy_applied;
volatile int cheeseburger_upstream_policy_status=-1;
int cheeseburger_boot_watchdog_policy(void *process, uint64_t scratch) {
 cheeseburger_watchdog_policy_status=0;
#if DEVELOPMENT || DEBUG
 proc_t p=process;int enabled=0,upstream=0,error=0,fd=-1;int32_t result=0;
 cb_board_arg("cheeseburger-disable-watchdogd",&enabled,sizeof(enabled));
 cb_board_arg("cheeseburger-upstream-userland",&upstream,sizeof(upstream));
 unsigned mask=cheeseburger_policy_mask(enabled,upstream);
 if(!mask)return 0;
 const char *policy=(mask==7)?cheeseburger_upstream_xml:cheeseburger_watchdog_xml;
 size_t policy_bytes=strlen(policy);
 cheeseburger_upstream_policy_requested=(mask==7);
 cheeseburger_watchdog_policy_requested=1;
 const char *directory="/private/var/db/com.apple.xpc.launchd";
 const char *path="/private/var/db/com.apple.xpc.launchd/disabled.plist";
 if((error=copyout(directory,scratch,strlen(directory)+1)))goto done;
 struct mkdir_args md={.path=scratch,.mode=0755};error=mkdir(p,&md,&result);
 if(error && error!=EEXIST)goto done;
 if((error=copyout(path,scratch,strlen(path)+1)))goto done;
 // New volatile RAM seed has no policy: refuse conflicts rather than overwrite.
 struct open_args op={.path=scratch,.flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,.mode=0644};
 if((error=open(p,&op,&result)))goto done;fd=result;
 if((error=copyout(policy,scratch+0x200,policy_bytes)))goto done;
 struct write_args wr={.fd=fd,.cbuf=scratch+0x200,.nbyte=policy_bytes};user_ssize_t written=0;
 if((error=write(p,&wr,&written)))goto done;
 if(written!=(user_ssize_t)(policy_bytes)){error=EIO;goto done;}
 struct fchown_args owner={.fd=fd,.uid=0,.gid=0};if((error=fchown(p,&owner,&result)))goto done;
 struct fchmod_args mode={.fd=fd,.mode=0644};if((error=fchmod(p,&mode,&result)))goto done;
 struct fsync_args sync={.fd=fd};if((error=fsync(p,&sync,&result)))goto done;
 struct fstat64_args sa={.fd=fd,.ub=scratch+0x800};struct stat64 st={0};
 if((error=sys_fstat64(p,&sa,&result)) || (error=copyin(sa.ub,&st,sizeof(st))))goto done;
 if(st.st_uid!=0 || st.st_gid!=0 || !S_ISREG(st.st_mode) || (st.st_mode&07777)!=0644 || st.st_size!=(off_t)(policy_bytes)){error=EINVAL;goto done;}
 {struct close_args cl={.fd=fd};error=sys_close(p,&cl,&result);fd=-1;if(error)goto done;}
 const char *marker="/private/var/db/com.apple.xpc.launchd/disabled.migrated";
 if((error=copyout(marker,scratch,strlen(marker)+1)))goto done;
 struct open_args mark={.path=scratch,.flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,.mode=0644};
 if((error=open(p,&mark,&result)))goto done;fd=result;
 owner.fd=fd;mode.fd=fd;sync.fd=fd;sa.fd=fd;
 if((error=fchown(p,&owner,&result)) || (error=fchmod(p,&mode,&result)) || (error=fsync(p,&sync,&result)))goto done;
 if((error=sys_fstat64(p,&sa,&result)) || (error=copyin(sa.ub,&st,sizeof(st))))goto done;
 if(st.st_uid!=0 || st.st_gid!=0 || !S_ISREG(st.st_mode) || (st.st_mode&07777)!=0644 || st.st_size!=0)error=EINVAL;
 done:
 if(fd>=0){struct close_args cl={.fd=fd};int e=sys_close(p,&cl,&result);if(!error)error=e;}
 cheeseburger_watchdog_policy_status=error;
 if(!error)cheeseburger_watchdog_policy_applied=1;
 if(cheeseburger_upstream_policy_requested){cheeseburger_upstream_policy_status=error;cheeseburger_upstream_policy_applied=!error;}
 printf("P21-UP1 RAM launch policy mask=%u status=%d; file publication only, launchd acceptance pending\n",mask,error);
 printf("cheeseburger development-only watchdogd job isolation: %d (hardware watchdog unsupported)\n",error);
 return error;
#else
 (void)process;(void)scratch;return 0;
#endif
}
