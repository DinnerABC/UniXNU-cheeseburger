# OnePlus 5 native touch

P22 adds an opt-in native MSM8998 QUP5 / Synaptics RMI4 F12 producer to the
existing official HID consumer. It does not replace the working P21-UP1 display,
Data, signed OS components, keystore or storage path.

## Frozen baseline

The user confirmed the original iPadOS UI and time with UP1. This is a user
observation, not a retrieved touch trace. The preserved image is
`out/p21/upstream-a/uefi/boot-p21-up1.img`, SHA256
`7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`.
Kernel SHA256:
`bb711854df65705a7e5fb28f56ea132f183e3b84ad8aa8cbd9f05937d814f662`.
The original ADT and RAM Data are copied byte-for-byte into P22; detailed hashes
are in `out/p22/up1-baseline.json`. No P22 Git push is authorized.

## Sources and board binding

The official input architecture is from UniXNU framework
`16e592abee8a550dec8154ed1f38b5ebc4f16213`, `docs/porting/input.md`.
The existing NativeHidInput consumer retains the original IOHIDFamily ABI,
Touch Screen report descriptor and down/move/up dispatch. No Novatek firmware,
GENI SPI implementation or sheng coordinate constants are used.

The transport definitions and bounded protocol adaptation follow Linux commit
`6b4b543fa18757297ac756a6fe6e64f506b7844a`:

- `drivers/i2c/busses/i2c-qup.c`: QUP v2 tags, state machine, FIFO/block PIO,
  clock divider and error flags.
- `drivers/clk/qcom/gcc-msm8998.c`: QUP5 XO clock and local branch / AHB vote.
- `drivers/pinctrl/qcom/pinctrl-msm8998.c`, `pinctrl-msm.c`: EAST tile,
  GPIO mux, falling-edge configuration and status acknowledgement.
- `drivers/input/rmi4/rmi_driver.c`, `rmi_f01.c`, `rmi_f12.c`, `rmi_i2c.c`:
  PDT, paged I2C, register descriptors, sensor tuning and object layout.

References are stored under `out/p22/reference/`. New transport/protocol source
is GPL-2.0-only and identifies its Linux origins. The existing HID source keeps
its original license. This is a source adaptation, not a claim of upstream
hardware qualification on this device.

Actual Recovery FDT and input inventory identify S3320 at 7-bit address 0x20,
QUP5 0x0c179000, SDA87/SCL88, RESET89 and attention125. GPIO registers use TLMM
0x03400000 + EAST 0x900000 + GPIO*0x1000. Panel 1080x1920 is not the sensor range.

## Runtime behavior

`cheeseburger-touch=1` starts a separate bounded worker after existing HID
initialization. Disabled means no touch MMIO or worker. Clock parent/divider must
already describe XO/1; the driver refuses an unknown clock configuration rather
than retuning shared PLLs. It enables only the documented QUP5 branch and existing
BLSP AHB vote. It preserves regulator and reset output state and does not pulse
sensor reset. An asserted reset, missing power, NACK, timeout or unsupported
layout becomes a diagnostic failure; it cannot block UI boot or panic deliberately.

Transactions use QUP v2 combined write/repeated-start/read and PIO, with a 50ms
transaction budget. Larger input packets use hardware-derived block sizing.
There is no DMA, SMMU change or UEFI callback. Controller recovery is bounded;
three consecutive sample failures release touches and stop the worker.

PDT scanning is bounded to eight pages/two consecutive empty pages. F01 identifies
the manufacturer and rejects flash mode. F12 must advertise register descriptors.
Query/Control/Data descriptors are read at runtime and bounds checked. Ctrl8
subpacket0 supplies max X/Y. Data1 supplies the object count and size; this first
implementation accepts up to ten eight-byte objects. Unsupported or larger
layouts fail closed with a stage/error code; no guessed Ctrl23 offsets are used.
Packet register address offsets advance by one present register, distinct from
byte offsets within an aggregate attention packet. Each packet is read whole.
Only F01 normal/nosleep/configured and reported interrupt enables are writable.
F34, firmware download, calibration and flash operations are absent.

The producer accepts finger objects, maps each stable slot to Contact ID slot+1,
clamps coordinates and publishes one seqlock snapshot. Identical frames are
suppressed. The HID consumer retains its explicit up reports for vanished IDs.
Coordinates are normalized to the descriptor's 0..32767 logical range; the old
30480/20320 constants are removed. `cheeseburger-touch-transform` is a bitmask:
1 flip X, 2 flip Y, 4 swap XY. The default is 3, based on the OP5 orientation
reference; its physical direction remains to be checked by real touch.

The digitizer is created only after successful hardware/protocol discovery.
It reports I2C / Synaptics S3320 Touch Screen, Built-In, DisplayIntegrated and
DigitizerCollectionDispatch. HID report publication runs on its workloop;
producer callbacks retain their owner and do not hold the sink lock during HID
workloop execution.

Default sampling is 40Hz (25ms sleep). `cheeseburger-touch-irq=1` additionally
requests GPIO125 falling-edge -> TLMM summary SPI208 / GIC INTID240. It requires
the existing ArmCpu native SPI API and exclusive summary ownership; other enabled
GPIO interrupts cause fallback. ISR masks/acknowledges only GPIO125 and sets a
worker flag; no bus/HID work runs there. A periodic poll remains as recovery.
Default candidate keeps IRQ opt-in because routing is not yet hardware verified.

## Diagnostics and validation

The existing bounded persistent-log window is unchanged. Three compact touch
rows capture init/error, bus success/error, F01/F12, sensor range, objects,
frames/contact count, publications/HID reports, poll/IRQ counts and last positions,
plus the last QUP clock/status/error snapshot. There is no per-byte/frame printing.

`out/p22/tests/rmi-host.cpp` runs with AddressSanitizer/UBSan and covers PDT,
descriptors, F01/F12, page changes, combined read tags and lengths, multiple fingers,
release/invalid objects, clipping, orientation and bus errors. Host results do not
prove pinmux, power, physical touches or backboardd delivery.

Build uses `scripts/p22-compile.py` and the existing official kernel/prelink path,
`-j2`, with `CheeseburgerTouch` genuinely in the board archive and the HID producer
macro enabled. One candidate is packaged under `out/p22/`. Final manifest and
boot result, rather than these design notes, determine the achieved evidence level.

## Candidate built

The complete native kernel, original-component prelink and temporary-boot firmware
were built. Packed input checks and `P22_NATIVE_TOUCH_OFFLINE_INPUT_PASS` succeeded.
The final native kernel contains the touch probe, producer snapshot, HID start
function and native ArmCpu SPI entry. Source snapshots, build commands, patch and
image/kernel hashes are retained in `out/p22/`. The blue observer adds only two
touch rows, alongside three bounded persistent rows.

The OP5 Windows reference was verified at tree
`5c90b2f534a370f3d3286bb24d6d5c5e7ca271c5`, INF blob
`031d0d052f87012663313834d9f385cf2eca50f7`: InvertXAxis=1,
InvertYAxis=1, SwapAxes=0. This supports transform3 as a first candidate, not a
claim of physically verified orientation.

UP1 log retrieval `20261026T130815Z` preserved 745 snapshot commits, zero write
errors, and observed maximum SWAP check75/reject0/transfer75. This confirms previous
display activity; it does not validate P22 touch.

## R2 resources and concrete clock correction

P22 raw logs20261026T134118Z show error-51, clock0, zero I2C/read/HID counts;
touch initialization stopped before sensor access. Linux at the same fixed pin,
`drivers/clk/qcom/clk-rcg2.c:calc_rate`, leaves parent_rate unchanged when HID
divider is0. R2 accepts sourceXO with divider0 or1, both19.2MHz, preserving RCG
and PLL values. Other parents/dividers remain rejected.

The user requested increased resources. Hardware inventory identifies a6GiB
unit and eight CPU affinities0/1/2/3/0x100/0x101/0x102/0x103. R2 requests a
4GiB contiguous loader allocation and sets kern.maxvnodes66560, the official OS
profile value, instead of17408. Physical inventory and reserved ranges remain
unchanged. All6GiB cannot be requested as one free contiguous region because the
firmware/security/display allocations split it. Four GiB is a candidate request,
not a measurement of the maximum current UEFI free run; allocation remains
checked by the official loader. No reserved pages are reclaimed.

The actual compiled template had MAX_CPUS4/MAX_CPU_CLUSTERS1. Patch0007 raises
these to8/2 and selects existing XNU HAS_CLUSTER paths so reset entry preserves
Affinity1. NO_CPU_OVRD1 excludes an Apple-specific register unsupported on this
Qualcomm target. Existing native ArmCpu handles PSCI SMC, GIC redistributors,
architectural timers and SGIs; the candidate enables sheng-smp and self-IPI.
Actual online count uses processor_avail_count, not requested boot arguments.
Per-core PSCI return, stage, failures, timer and IPI counts go to persistent logs.
Core capacity preprocessing is recorded in compiled-capacity.json. Generic SMP
scheduling is retained; this does not claim frequency/DVFS or tuned big.LITTLE
performance.

The diagnostic scanline and OS scanout now acquire the same atomic reservation.
Local IRQ masking alone did not exclude writes from another core. This change
protects the established CPU display copy without altering its pixel/scaling code.
The sparse1GiB RAM Data format, keystore, original signed components, storage
policy and bounded32MiB log window remain unchanged. Other memory-scaled kernel
defaults keep XNU sizing from the handed-off memory; arbitrary mbuf/process/file
limit overrides were not added. Runtime8-core operation remains to be verified.

R2 startup-order audit also found that IOCPUInterruptController::registerInterrupt
waits for enabledCPUs==numCPUs, but the native driver starts secondaries only after
this registration. Its existing secondary path installs handlers directly. The
board-local component patch therefore gives the shared controller one boot-CPU
slot in SMP mode, retaining the existing direct secondary handlers. Single-core
behavior is unchanged. The fixed upstream checkout is not edited; the explicit
patch is applied to the resolved component before its normal source build.
