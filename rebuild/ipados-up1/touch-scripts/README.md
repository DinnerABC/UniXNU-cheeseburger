# iPadOS UP1 touch + resources candidate

Baseline: source commit `f89c28c9f83ec171ba7ee43018b0cd20766d9698`; import the current tracked P22 touch producer, HID integration, logging and board runtime build script via `prepare-source.py`. The candidate uses the P22 `0007` XNU patch, isolated ArmCpu SMP patch, and cross-core DisplayObservation. The source tree and build records are retained in `out/p21/touch-up1/source/`.

The candidate ADT starts from UP1 and takes only `cpus`, `/psci`, `chosen/darwinboot-memory-size` and `defaults/kern.maxvnodes` from the local iOS 16 Stage C ADT. Values: 8 affinities (0,1,2,3,0x100,0x101,0x102,0x103), PSCI SMC, 2 GiB, 33792 vnodes. Preserve iPadOS root UUID, trust, data and display identity. Config adds `cheeseburger-touch=1`, `cpus=8`, `sheng-smp=1`, and `armcpu-self-ipi=1`. The iPadOS 2 GiB boot path remains unverified and an earlier P22 2 GiB attempt reported NO WINDOW.

Before running `scripts/p22-compile-r5.py`, materialize an independent copy of the resolved `tree/drivers/ArmCpu` symlink target, because the script patches that tree; do not patch the pinned upstream checkout. The remaining scripts are adapted to this candidate's paths and must not overwrite UP1 artifacts. Firmware source still depends on the preserved VM overlay chain.

The GPIO89 input pull-up follows the OnePlus 5 mainline device tree and Linux pinctrl. It may not restore power or deassert reset if an external driver holds the line low; verify `TOUCH` stage, error, I2C transactions and HID events in the next boot log. Host tests do not establish physical touch.
