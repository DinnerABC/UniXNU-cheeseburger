# Experimental phone UI identity

The iPadOS 17.7.11 root stays unchanged. The candidate keeps the working OnePlus 5 compatible, boot, storage, CPU, memory, display, touch and kernel inputs and substitutes only model, target and product identity fields from the local iPhone 10,3 ADT. This is a user-requested experiment outside the official recommendation that OS roots and device identity match. It may not produce a usable phone UI; MobileGestalt caches may retain previous answers. The data seed and filesystem are not modified or erased by this experiment. The OnePlus home button / key identity is preserved rather than borrowing the iPhone X button node.

`make-adt.py` refuses to overwrite a previous output. After generating the ADT, rebuild payload and firmware pins, then validate the resulting image. The exact candidate is `out/p21/phone-ui`; scripts and VM overlay are preserved locally. See `docs/guide/mobilegestalt.md` in pinned UniXNU for field meanings and cache behavior.
