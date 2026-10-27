#!/usr/bin/env python3
"""Add the iOS16/XNU8796 OOL-ports-array reply for mach_ports_lookup (id 3404)
to the generated MIG task server, selected by ios16_legacy_mach_ports.
The current XNU10063 three-port reply remains for non-legacy boots."""
from pathlib import Path
import sys
path = Path(sys.argv[1]); text = path.read_text()
if 'p23_old_lookup_reply_t' in text:
    print('lookup already patched', path); raise SystemExit(0)
marker = 'extern kern_return_t ios16_mach_ports_register(task_t, mach_port_array_t, mach_msg_type_number_t);\n'
decl = marker + '''
/* P23 iOS16 launchd cache loader: XNU8796 mach_ports_lookup reply layout. */
typedef struct {
	mach_msg_header_t Head;
	mach_msg_body_t msgh_body;
	mach_msg_ool_ports_descriptor_t init_port_set;
	NDR_record_t NDR;
	mach_msg_type_number_t init_port_setCnt;
} p23_old_lookup_reply_t;
extern int ios16_legacy_mach_ports;
'''
if marker not in text:
    raise SystemExit('register extern anchor missing')
text = text.replace(marker, decl, 1)

old = '''\tOutKP->port1.name = port1;

\tOutKP->port2.name = port2;

\tOutKP->port3.name = port3;


\tOutKP->Head.msgh_bits |= MACH_MSGH_BITS_COMPLEX;
\tOutKP->Head.msgh_size = (mach_msg_size_t)(sizeof(Reply));
\tOutKP->msgh_body.msgh_descriptor_count = 3;
'''
new = '''\tif (ios16_legacy_mach_ports) {
\t\tp23_old_lookup_reply_t *old = (p23_old_lookup_reply_t *)OutKP;
\t\tmach_port_array_t arr = mach_port_array_alloc(3, Z_WAITOK | Z_ZERO);
\t\tif (arr == NULL) {
\t\t\tMIG_RETURN_ERROR(OutKP, KERN_RESOURCE_SHORTAGE);
\t\t}
\t\tarr[0].port = port1;
\t\tarr[1].port = port2;
\t\tarr[2].port = port3;
\t\told->init_port_set.address = arr;
\t\told->init_port_set.count = 3;
\t\told->init_port_set.deallocate = FALSE;
\t\told->init_port_set.copy = MACH_MSG_PHYSICAL_COPY;
\t\told->init_port_set.disposition = 17;
\t\told->init_port_set.type = MACH_MSG_OOL_PORTS_DESCRIPTOR;
\t\told->init_port_setCnt = 3;
\t\told->NDR = NDR_record;
\t\told->Head.msgh_bits |= MACH_MSGH_BITS_COMPLEX;
\t\told->Head.msgh_size = (mach_msg_size_t)sizeof(p23_old_lookup_reply_t);
\t\told->msgh_body.msgh_descriptor_count = 1;
\t} else {
\t\tOutKP->port1.name = port1;

\t\tOutKP->port2.name = port2;

\t\tOutKP->port3.name = port3;


\t\tOutKP->Head.msgh_bits |= MACH_MSGH_BITS_COMPLEX;
\t\tOutKP->Head.msgh_size = (mach_msg_size_t)(sizeof(Reply));
\t\tOutKP->msgh_body.msgh_descriptor_count = 3;
\t}
'''
if old not in text:
    raise SystemExit('lookup reply anchor missing')
text = text.replace(old, new, 1)
path.write_text(text); print('lookup patched', path)
