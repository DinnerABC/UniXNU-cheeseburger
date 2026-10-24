// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
// UFS response layout/flags cross-checked against Linux v6.12 include/ufs/ufs.h.
// Only REPORT LUNS accepts a short data-in response. Block reads stay exact.
struct P11UfsResponse { unsigned transferred=0, residual=0, senseBytes=0; };
static inline unsigned p11UfsBe32(const unsigned char *p) {
 return unsigned(p[0])<<24|unsigned(p[1])<<16|unsigned(p[2])<<8|p[3];
}
static inline bool p11UfsResponse(const unsigned char *r,unsigned available,
 unsigned lun,unsigned opcode,unsigned requested,P11UfsResponse &out) {
 out=P11UfsResponse{};
 if(available<52||r[0]!=0x21||r[2]!=lun||r[3]!=1||r[4]||r[6]||r[7]||r[8])return false;
 unsigned segment=unsigned(r[10])<<8|r[11];
 out.senseBytes=unsigned(r[32])<<8|r[33];
 if(segment>available-32||out.senseBytes>18||
    (segment ? segment<2+out.senseBytes : out.senseBytes!=0))return false;
 // CHECK CONDITION and nonempty sense never become success.
 if(out.senseBytes||(r[1]&~0x20U))return false;
 out.residual=p11UfsBe32(r+12);
 if(out.residual>requested)return false;
 if(opcode!=0xa0&&(out.residual||(r[1]&0x20)))return false;
 out.transferred=requested-out.residual;
 return opcode!=0xa0||out.transferred>=8;
}
static inline bool p11UfsLunList(const unsigned char *data,unsigned received) {
 if(received<8)return false;
 unsigned n=p11UfsBe32(data);
 return !(n&7)&&n<=248&&n<=received-8;
}
