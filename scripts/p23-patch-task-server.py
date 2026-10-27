#!/usr/bin/env python3
"""Add the iOS16 launchd (XNU8796-style) mach_ports_register request layout to the
generated MIG task server, while keeping the current 3-port layout.

The old request is one MACH_MSG_OOL_PORTS_DESCRIPTOR plus NDR and count; the current
request is three inline MACH_MSG_PORT_DESCRIPTORs. The kernel business call is the
same after translating the old array. Run after MIG generates task_server.c.
"""
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
if 'p23_old_register_request_t' in text:
    print('already patched', path)
    raise SystemExit(0)
marker = '#include "task_server.h"\n'
decl = marker + '''
/* P23 iOS16 launchd compatibility: XNU8796 mach_ports_register wire layout. */
typedef struct {
	mach_msg_header_t Head;
	mach_msg_body_t msgh_body;
	mach_msg_ool_ports_descriptor_t init_port_set;
	NDR_record_t NDR;
	mach_msg_type_number_t init_port_setCnt;
} p23_old_register_request_t;
extern kern_return_t ios16_mach_ports_register(task_t, mach_port_array_t, mach_msg_type_number_t);
'''
if marker not in text:
    raise SystemExit('task_server.h include missing')
text = text.replace(marker, decl, 1)

old_check = '''\tif (!(InKP->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
\t    (InKP->msgh_body.msgh_descriptor_count != 3) ||
\t    (InKP->Head.msgh_size != (mach_msg_size_t)sizeof(__Request)))
\t\treturn MIG_BAD_ARGUMENTS;
'''
new_check = '''\tif (!(InKP->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX))
\t\treturn MIG_BAD_ARGUMENTS;
\tif (InKP->msgh_body.msgh_descriptor_count == 3 &&
\t    InKP->Head.msgh_size == (mach_msg_size_t)sizeof(__Request)) {
\t\t/* current XNU10063 layout */
'''
if old_check not in text:
    raise SystemExit('new-layout check anchor missing')
text = text.replace(old_check, new_check, 1)

old_descs = '''\tif (InKP->port1.type != MACH_MSG_PORT_DESCRIPTOR ||
\t    InKP->port1.disposition != 17)
\t\treturn MIG_TYPE_ERROR;
#endif\t/* __MigTypeCheck */

#if\t__MigTypeCheck
\tif (InKP->port2.type != MACH_MSG_PORT_DESCRIPTOR ||
\t    InKP->port2.disposition != 17)
\t\treturn MIG_TYPE_ERROR;
#endif\t/* __MigTypeCheck */

#if\t__MigTypeCheck
\tif (InKP->port3.type != MACH_MSG_PORT_DESCRIPTOR ||
\t    InKP->port3.disposition != 17)
\t\treturn MIG_TYPE_ERROR;
#endif\t/* __MigTypeCheck */

\treturn MACH_MSG_SUCCESS;
'''
new_descs = '''\tif (InKP->port1.type != MACH_MSG_PORT_DESCRIPTOR ||
\t    InKP->port1.disposition != 17)
\t\treturn MIG_TYPE_ERROR;
#endif\t/* __MigTypeCheck */

#if\t__MigTypeCheck
\tif (InKP->port2.type != MACH_MSG_PORT_DESCRIPTOR ||
\t    InKP->port2.disposition != 17)
\t\treturn MIG_TYPE_ERROR;
#endif\t/* __MigTypeCheck */

#if\t__MigTypeCheck
\tif (InKP->port3.type != MACH_MSG_PORT_DESCRIPTOR ||
\t    InKP->port3.disposition != 17)
\t\treturn MIG_TYPE_ERROR;
#endif\t/* __MigTypeCheck */
\t\treturn MACH_MSG_SUCCESS;
\t}
\tif (InKP->msgh_body.msgh_descriptor_count == 1 &&
\t    InKP->Head.msgh_size == (mach_msg_size_t)sizeof(p23_old_register_request_t)) {
\t\tp23_old_register_request_t *old = (p23_old_register_request_t *)InKP;
\t\tif (old->init_port_set.type != MACH_MSG_OOL_PORTS_DESCRIPTOR ||
\t\t    old->init_port_set.disposition != 17 ||
\t\t    old->init_port_set.count != old->init_port_setCnt ||
\t\t    old->init_port_setCnt > 3)
\t\t\treturn MIG_TYPE_ERROR;
\t\treturn MACH_MSG_SUCCESS;
\t}
\treturn MIG_BAD_ARGUMENTS;
'''
if old_descs not in text:
    raise SystemExit('descriptor check anchor missing')
text = text.replace(old_descs, new_descs, 1)

old_call = '\tOutUP->RetCode = _kernelrpc_mach_ports_register3(target_task, InKP->port1.name, InKP->port2.name, InKP->port3.name);\n'
new_call = '''\tif (InKP->msgh_body.msgh_descriptor_count == 1 &&
\t    InKP->Head.msgh_size == (mach_msg_size_t)sizeof(p23_old_register_request_t)) {
\t\tp23_old_register_request_t *old = (p23_old_register_request_t *)InKP;
\t\tOutUP->RetCode = ios16_mach_ports_register(target_task,
\t\t    (mach_port_array_t)old->init_port_set.address, old->init_port_set.count);
\t} else {
\t\tOutUP->RetCode = _kernelrpc_mach_ports_register3(target_task, InKP->port1.name, InKP->port2.name, InKP->port3.name);
\t}
'''
if old_call not in text:
    raise SystemExit('business call anchor missing')
text = text.replace(old_call, new_call, 1)
path.write_text(text)
print('patched', path)
