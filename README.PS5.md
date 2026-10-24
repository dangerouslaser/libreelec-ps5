# LibreELEC CB1 for the PlayStation 5 / PS5 Pro

This is CroqueMr's [libreelec-x86-DV](https://github.com/CroqueMr/libreelec-x86-DV)
(LibreELEC 13 with the CB1 Dolby Vision engine) with an added `PS5` device. The device
builds the [linux-ps5](https://github.com/dangerouslaser/linux-ps5) kernel:
Linux 7.3 with PS5 support and CB1's AMD Dolby Vision ported to the PS5's
DisplayPort-to-HDMI converter.

CroqueMr's history is unchanged on `main`. The PS5 work is on the `ps5` branch.

## Build

```sh
PROJECT=Generic DEVICE=PS5 ARCH=x86_64 make image
```

The PS5 device:

- sets `LINUX=ps5`, i.e. `packages/linux/package.mk` builds linux-ps5 at a pinned commit;
- uses `projects/Generic/devices/PS5/linux/linux.x86_64.conf`, which is the Generic config plus the PS5 platform,
  its ethernet (MTS), Salina SATA, MT7925 Wi-Fi, simpledrm and kexec;
- builds `devices/PS5/firmware/amdgpu/ps5_vcn.bin` into the kernel, from ps5-linux-patches.

CB1's project kernel patches move to `projects/Generic/patches/linux/default`. They still apply
to the stock Generic kernel. The PS5 kernel already contains the AMD and generic DV
parts, so they are skipped there.

## Status

The kernel and CB1 Kodi are verified on a PS5 Pro with an LG B4:

- 1917 (DV profile 7 FEL) plays as TV-led Dolby Vision at 4K 23.976 Hz with TrueHD Atmos passthrough. The Kodi OSD works.
- Known issue: with Dolby Vision active, AC3/E-AC3 *bitstream* passthrough makes the LG drop
  DV 9 s after it starts. Pick the TrueHD track, or let Kodi decode to PCM.
- Booting this LibreELEC image directly on the PS5 is untested. Testing so far ran CB1's
  Kodi in a chroot on Ubuntu with the same kernel (see `tools/ps5`).

Kodi settings that matter:

- Speakers 7.1 is required for TrueHD passthrough.
- Dolby Vision output mode: *Dolby Vision Standard*.
- The kernel defaults the AVI content type to *cinema*, so LG TVs stay out of their ALLM game modes.

## Credits

- **LibreELEC:** Team LibreELEC (GPL-2.0).
- **CB1 Dolby Vision engine and this LibreELEC tree:** CroqueMr. See
  `CB1/LICENSE`, `CB1/src/PROVENANCE.json` and `CB1/docs/LICENSE-MAP.md` for CB1's own licensing and the
  work it builds on, including LibreELEC Hades (MIT), the YBLOD transport, intel-dv-linux,
  DoViBaker and libplacebo.
- **PS5 Linux:** [ps5-linux](https://github.com/ps5-linux): Andy Nguyen (TheFloW), rmux, Slip,
  masterzorag, sleirsgoevy and the other contributors.
- **PS4 MN86471A bridge driver register names:** [fail0verflow](https://github.com/fail0verflow/ps4-linux).
- **PS5 device, kernel port, converter fixes and tools:** Bryan Hoban.
