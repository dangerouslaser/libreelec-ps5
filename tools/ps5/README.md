# PS5 tools

Debugging and bring-up tools from getting Dolby Vision working on the PS5 Pro.
Each one runs on the PS5 under the `linux-ps5` kernel.

| Tool | What it does |
| --- | --- |
| `ps5hdmi.py` | Sends command batches (read, write, mask, wait) to the DP-to-HDMI converter through `debugfs ps5_hdmi/icc`, e.g. `ps5hdmi.py --code 1 "r 72c0 16"`. |
| `dvcrc.c` | Takes the display and shows a known linear frame (pattern or raw file), optionally in Dolby Vision mode and flipping between two frames. It compares the converter's DP receiver CRC with the CRC computed from the framebuffer, which proves the GPU→converter path is bit-exact. |
| `fbgrab-gl.c` | Dumps the (tiled) framebuffer currently on a plane, de-tiled on the GPU through EGL. It prints the same DP CRC. |
| `dvdecode.py` | Decodes the CB1 Dolby Vision tunnel metadata from a grabbed frame and checks each packet's CRC-32. |
| `le-kodi.sh`, `le-kodi.service` | Run a LibreELEC SYSTEM image's Kodi (e.g. CB1) in a chroot on a normal distro, on the PS5 kernel. |
| `kj.sh` | Minimal Kodi JSON-RPC client (webserver on 127.0.0.1:8080). |

Build: `gcc -O2 -o dvcrc dvcrc.c -I/usr/include/libdrm -ldrm` and
`gcc -O2 -o fbgrab-gl fbgrab-gl.c -I/usr/include/libdrm -ldrm -lEGL -lGLESv2 -lgbm`.

Converter register names come from fail0verflow's PS4 MN86471A driver
(UPDCTRL 0x7011, CSCMOD 0x70c0, INFENA 0x7203). The CRC algorithm follows
DP 1.4 appendix J, as in IGT's `igt_fb_calc_crc`.
