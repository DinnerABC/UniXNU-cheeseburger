// Bounded named UI exit diagnostics; original exit disposition is unchanged.
static const char *ui_names[4]={"PreBoard","backboardd","SpringBoard","watchdogd"};
static unsigned ui_gate,ui_exits[4],ui_details[4],ui_abort_seen[4];
static char ui_abort[4][270],ui_fault[4][270];
char p20_ui_rows[24][91];
static int ui_slot(const char *name){for(int i=0;i<4;++i)if(name&&!strcmp(name,ui_names[i]))return i;return -1;}
static void p20_ui_abort_payload(const char *name,unsigned ns,uint64_t code,uint64_t reason) {
 int slot=ui_slot(name);if(slot<0 || __atomic_exchange_n(&ui_gate,1,__ATOMIC_ACQUIRE))return;
 if(ui_abort_seen[slot]){__atomic_store_n(&ui_gate,0,__ATOMIC_RELEASE);return;}ui_abort_seen[slot]=1;
 char text[161]={0};unsigned copied=0;int error=0;
 if(reason)for(;copied<160;++copied){error=copyin(reason+copied,text+copied,1);if(error||!text[copied])break;if((unsigned char)text[copied]<32||(unsigned char)text[copied]>126)text[copied]='?';}
 snprintf(ui_abort[slot],sizeof(ui_abort[slot]),"ABORT p%d ns=%u code=%llx bytes=%u err=%d: %s",proc_selfpid(),ns,code,copied,error,text);
 __atomic_store_n(&ui_gate,0,__ATOMIC_RELEASE);
 printf("\nFIRST UI ABORT %s: %s\n",ui_names[slot],ui_abort[slot]);
}
static void p20_ui_on_exit(int pid,const char *name,int status) {
 int slot=ui_slot(name);unsigned signal=(unsigned)status&0x7f;
 if(slot<0 || signal<3 || signal>12 || signal==9)return;
 __atomic_fetch_add(&ui_exits[slot],1,__ATOMIC_RELAXED);
 if(!ml_get_interrupts_enabled() || __atomic_exchange_n(&ui_gate,1,__ATOMIC_ACQUIRE))return;
 unsigned first=ui_details[slot]==0;
 if(first){++ui_details[slot];boot_progress_exit_fault(pid,signal,ui_fault[slot],sizeof(ui_fault[slot]));if(!ui_fault[slot][0])snprintf(ui_fault[slot],sizeof(ui_fault[slot]),"p%d sig%u: saved registers unavailable",pid,signal);}
 __atomic_store_n(&ui_gate,0,__ATOMIC_RELEASE);
 if(first)printf("\nFIRST UI FAULT %s: %s\n",ui_names[slot],ui_fault[slot]);
}
static void p20_ui_snapshot(void) {
 if(__atomic_exchange_n(&ui_gate,1,__ATOMIC_ACQUIRE))return;
 memset(p20_ui_rows,0,sizeof(p20_ui_rows));
 for(unsigned slot=0;slot<4;++slot){unsigned row=slot*6;snprintf(p20_ui_rows[row],91,"%s fault exits=%u details=%u",ui_names[slot],ui_exits[slot],ui_details[slot]);
  const char *text=ui_abort[slot][0]?ui_abort[slot]:"No direct abort reason captured";size_t len=strnlen(text,269),at=0;
  for(unsigned j=1;j<=3;++j){size_t n=len-at;if(n>90)n=90;memcpy(p20_ui_rows[row+j],text+at,n);at+=n;}
  text=ui_fault[slot][0]?ui_fault[slot]:"No matching fatal-signal exit captured";len=strnlen(text,269);at=0;
  for(unsigned j=4;j<=5;++j){size_t n=len-at;if(n>90)n=90;memcpy(p20_ui_rows[row+j],text+at,n);at+=n;}
 }
 __atomic_store_n(&ui_gate,0,__ATOMIC_RELEASE);
}
