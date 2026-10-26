# Local MSM8998 migration profile

Source: captured recovery-device.dtb SHA256 743b9d0fee2a7aedb9db32dc57ddfb8197378b78ea003d8c653f492b7e5a2448 and qualified R7 ADT. Topology: 0,1,2,3,0x100,0x101,0x102,0x103; timer19.2MHz; only CPU0 enabled in inventory DTS. GIC/UFS ranges are board-qualified inputs, not sheng values.

This profile is unpublished and incomplete. Common ADT conversion currently requires one contiguous DRAM range, whereas the captured board inventory has two banks. Its binding whitelist also omits the qualified p8,* contract, and CPU compatible emission differs from the working original-AppleCPU boundary. Do not boot a centrally generated ADT until all differences are resolved and gated. The transitional official OS-property adapter preserves the working hardware ADT.
