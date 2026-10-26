// Generated from out/p21/r14/log-window.json; live GPT and two matching backups.
#pragma once
#define CB_LOG_FIRST 5062912ULL
#define CB_LOG_BLOCKS 8192ULL
#define CB_LOG_USER_FIRST 2441472ULL
#define CB_LOG_USER_LAST 14145279ULL
#define CB_LOG_GPT_HEADER_CRC 1079187191U
#define CB_LOG_GPT_ENTRIES_CRC 2823248467U
static const unsigned char cb_log_gpt_hash[32]={0x4d,0x96,0xee,0x47,0x53,0x90,0x89,0x2a,0x47,0xbe,0x87,0x54,0x98,0x2a,0x3e,0x03,0xe4,0xbc,0xae,0xc3,0xf5,0x60,0x5a,0x23,0xb5,0x53,0x7e,0x76,0x8b,0x25,0x95,0x00};
static const unsigned char cb_log_empty_hash[32]={0x83,0xee,0x47,0x24,0x53,0x98,0xad,0xee,0x79,0xbd,0x9c,0x0a,0x8b,0xc5,0x7b,0x82,0x1e,0x92,0xab,0xa1,0x0f,0x5f,0x9a,0xde,0x8a,0x5d,0x1f,0xae,0x4d,0x8c,0x43,0x02};
static inline bool cbLogRange(unsigned long long lba,unsigned count){return count && count<=32 && lba>=CB_LOG_FIRST && lba-CB_LOG_FIRST<CB_LOG_BLOCKS && count<=CB_LOG_BLOCKS-(lba-CB_LOG_FIRST);}

static inline bool cbLogCommand(const unsigned char *cdb,unsigned lun,unsigned dir,unsigned bytes){
 if(lun)return false;
 if(cdb[0]==0x35){if(dir||bytes)return false;for(unsigned i=1;i<16;++i)if(cdb[i])return false;return true;}
 if(cdb[0]!=0x2a||dir!=1||!bytes||bytes>131072||bytes%4096||cdb[1]||cdb[6]||cdb[9])return false;
 for(unsigned i=10;i<16;++i)if(cdb[i])return false;
 unsigned long long lba=(unsigned long long)cdb[2]<<24|(unsigned long long)cdb[3]<<16|(unsigned long long)cdb[4]<<8|cdb[5];unsigned n=(unsigned)cdb[7]<<8|cdb[8];
 return cbLogRange(lba,n)&&bytes==n*4096;
}
