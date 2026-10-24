// dvcrc: show a known linear XRGB8888 frame on DP-1 in the DV Standard
// configuration and compare the converter's DP RX CRC with the CRC computed
// from the framebuffer (DP 1.4 appendix J, as in IGT igt_fb_calc_crc).
//
// usage: dvcrc [-d 0|1] [-p noise|ramp|solid] [-w HZ] [-s SECONDS]
// build: gcc -O2 -o dvcrc dvcrc.c -I/usr/include/libdrm -ldrm
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define CRC_DIR "/sys/kernel/debug/dri/0/crtc-0/crc"

static uint16_t crc16_dp(uint16_t b, uint16_t d)
{
	/* Bit-serial form of f(x) = x^16 + x^15 + x^2 + 1, MSB first; equal to
	 * IGT's parallel update_crc16_dp. */
	for (int i = 15; i >= 0; i--) {
		int fb = ((b >> 15) ^ (d >> i)) & 1;
		b <<= 1;
		if (fb)
			b ^= 0x8005;
	}
	return b;
}

static int fd;

static uint32_t prop_id(uint32_t obj, uint32_t type, const char *name,
			uint64_t *enum_val, const char *enum_name)
{
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
	uint32_t id = 0;

	for (uint32_t i = 0; props && i < props->count_props; i++) {
		drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
		if (p && !strcmp(p->name, name)) {
			id = p->prop_id;
			if (enum_name)
				for (int e = 0; e < p->count_enums; e++)
					if (!strcmp(p->enums[e].name, enum_name))
						*enum_val = p->enums[e].value;
		}
		drmModeFreeProperty(p);
	}
	drmModeFreeObjectProperties(props);
	return id;
}

static void add(drmModeAtomicReq *req, uint32_t obj, uint32_t type,
		const char *name, uint64_t val, const char *enum_name)
{
	uint32_t id = prop_id(obj, type, name, &val, enum_name);

	if (!id) {
		fprintf(stderr, "warning: no property %s on %u\n", name, obj);
		return;
	}
	drmModeAtomicAddProperty(req, obj, id, val);
}

static void wr(const char *path, const char *s)
{
	int f = open(path, O_WRONLY);
	if (f < 0 || write(f, s, strlen(s)) < 0)
		perror(path);
	close(f);
}

int main(int argc, char **argv)
{
	int dv = 1, hz = 24, secs = 0, opt;
	const char *pat = "noise", *file = NULL, *file2 = NULL;
	int dx = 0, dy = 0, nocrc = 0, exact = 0;

	while ((opt = getopt(argc, argv, "d:p:w:s:f:F:x:y:ne")) != -1) {
		if (opt == 'd') dv = atoi(optarg);
		else if (opt == 'p') pat = optarg;
		else if (opt == 'w') hz = atoi(optarg);
		else if (opt == 's') secs = atoi(optarg);
		else if (opt == 'f') file = optarg;
		else if (opt == 'F') file2 = optarg;
		else if (opt == 'x') dx = atoi(optarg);
		else if (opt == 'y') dy = atoi(optarg);
		else if (opt == 'n') nocrc = 1;
		else if (opt == 'e') exact = 1;
	}

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0 || drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
	    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		perror("drm");
		return 1;
	}
	drmModeRes *res = drmModeGetResources(fd);
	drmModeConnector *conn = NULL;
	for (int i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (conn->connection == DRM_MODE_CONNECTED && conn->connector_type == DRM_MODE_CONNECTOR_DisplayPort)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn) {
		fprintf(stderr, "no connected DP connector\n");
		return 1;
	}
	drmModeModeInfo *mode = NULL;
	for (int i = 0; i < conn->count_modes; i++) {
		drmModeModeInfo *m = &conn->modes[i];
		if (m->hdisplay == 3840 && m->vdisplay == 2160 && m->vrefresh == hz &&
		    !(m->flags & DRM_MODE_FLAG_INTERLACE)) {
			if (!mode || (hz == 24 && !exact && m->clock < mode->clock) ||
			    (exact && m->clock > mode->clock))
				mode = m; /* prefer 23.976 over 24 */
		}
	}
	if (!mode) {
		fprintf(stderr, "no 3840x2160@%d mode\n", hz);
		return 1;
	}
	uint32_t crtc_id = res->crtcs[0];
	drmModePlaneRes *pres = drmModeGetPlaneResources(fd);
	uint32_t plane_id = 0;
	for (uint32_t i = 0; i < pres->count_planes && !plane_id; i++) {
		drmModePlane *pl = drmModeGetPlane(fd, pres->planes[i]);
		uint64_t type = 0;
		drmModeObjectProperties *pp = drmModeObjectGetProperties(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE);
		for (uint32_t k = 0; k < pp->count_props; k++) {
			drmModePropertyRes *p = drmModeGetProperty(fd, pp->props[k]);
			if (!strcmp(p->name, "type"))
				type = pp->prop_values[k];
			drmModeFreeProperty(p);
		}
		if ((pl->possible_crtcs & 1) && type == DRM_PLANE_TYPE_PRIMARY)
			plane_id = pl->plane_id;
		drmModeFreeObjectProperties(pp);
		drmModeFreePlane(pl);
	}
	printf("connector %u crtc %u plane %u mode %s %u kHz\n", conn->connector_id,
	       crtc_id, plane_id, mode->name, mode->clock);

	struct drm_mode_create_dumb cd = { .width = 3840, .height = 2160, .bpp = 32 };
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) {
		perror("create dumb");
		return 1;
	}
	uint32_t fb_id;
	uint32_t handles[4] = { cd.handle }, pitches[4] = { cd.pitch }, offsets[4] = { 0 };
	if (drmModeAddFB2(fd, 3840, 2160, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fb_id, 0)) {
		perror("addfb2");
		return 1;
	}
	struct drm_mode_map_dumb md = { .handle = cd.handle };
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
	uint8_t *px = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);

	uint8_t *img = NULL;
	if (file) {
		/* raw BGRX 3840x2160, content moved by (dx, dy), edges clamped */
		FILE *f = fopen(file, "rb");
		img = malloc(3840 * 2160 * 4);
		if (!f || fread(img, 4, 3840 * 2160, f) != 3840 * 2160) {
			fprintf(stderr, "cannot read %s\n", file);
			return 1;
		}
		fclose(f);
	}
	uint16_t crc[3] = { 0, 0, 0 };
	uint32_t seed = 12345;
	for (int y = 0; y < 2160; y++) {
		uint8_t *row = px + (size_t)y * cd.pitch;
		for (int x = 0; x < 3840; x++) {
			uint8_t r, g, b;
			if (img) {
				int sx = x - dx, sy = y - dy;
				sx = sx < 0 ? 0 : sx > 3839 ? 3839 : sx;
				sy = sy < 0 ? 0 : sy > 2159 ? 2159 : sy;
				const uint8_t *s = img + ((size_t)sy * 3840 + sx) * 4;
				b = s[0]; g = s[1]; r = s[2];
			} else if (!strcmp(pat, "ramp")) {
				r = x; g = y; b = x + y;
			} else if (!strcmp(pat, "coord")) {
				/* absolute position: x = G[3:0]:R, y = G[7:4]:B */
				r = x; b = y; g = (x >> 8) | ((y >> 8) << 4);
			} else if (!strcmp(pat, "solid")) {
				r = 0x40; g = 0x80; b = 0xc0;
			} else {
				seed = seed * 1103515245 + 12345;
				r = seed >> 24; g = seed >> 16; b = seed >> 8;
			}
			row[x * 4 + 0] = b;
			row[x * 4 + 1] = g;
			row[x * 4 + 2] = r;
			row[x * 4 + 3] = 0xff;
			crc[0] = crc16_dp(crc[0], r << 8);
			crc[1] = crc16_dp(crc[1], g << 8);
			crc[2] = crc16_dp(crc[2], b << 8);
		}
	}
	printf("expected fb crc: %04x %04x %04x\n", crc[0], crc[1], crc[2]);

	uint32_t blob;
	drmModeCreatePropertyBlob(fd, mode, sizeof(*mode), &blob);
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	uint32_t c = conn->connector_id;
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", crtc_id, NULL);
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "max bpc", 8, NULL);
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "Broadcast RGB", 0, "Full");
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "Colorspace", 0, NULL);
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "HDR_OUTPUT_METADATA", 0, NULL);
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "DVBRIDGE_HDR10_DEEP_COLOR", 0, NULL);
	add(req, c, DRM_MODE_OBJECT_CONNECTOR, "DVBRIDGE_DV_STANDARD_LAB", dv, NULL);
	add(req, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", blob, NULL);
	add(req, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", fb_id, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", 0, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", 3840ULL << 16, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", 2160ULL << 16, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", 3840, NULL);
	add(req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", 2160, NULL);
	int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	printf("commit dv=%d: %d (%s)\n", dv, ret, ret ? strerror(errno) : "ok");
	if (ret)
		return 1;
	sleep(3);

	for (int s = 0; s < (nocrc ? 0 : 6); s++) {
		const char *src = (s & 1) ? "crtc" : "dprx";
		char line[256];
		wr(CRC_DIR "/control", src);
		FILE *f = fopen(CRC_DIR "/data", "r");
		for (int n = 0; f && n < 2 && fgets(line, sizeof(line), f); n++)
			printf("%s: %s", src, line);
		if (f)
			fclose(f);
	}
	if (!nocrc)
		wr(CRC_DIR "/control", "auto");
	if (file2 && secs) {
		/* Alternate with a second frame every refresh, like a playing video. */
		struct drm_mode_create_dumb cd2 = { .width = 3840, .height = 2160, .bpp = 32 };
		uint32_t fb2;
		drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd2);
		uint32_t h2[4] = { cd2.handle }, p2[4] = { cd2.pitch }, o2[4] = { 0 };
		drmModeAddFB2(fd, 3840, 2160, DRM_FORMAT_XRGB8888, h2, p2, o2, &fb2, 0);
		struct drm_mode_map_dumb md2 = { .handle = cd2.handle };
		drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md2);
		uint8_t *px2 = mmap(NULL, cd2.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md2.offset);
		FILE *f = fopen(file2, "rb");
		for (int y = 0; f && y < 2160; y++)
			if (fread(px2 + (size_t)y * cd2.pitch, 4, 3840, f) != 3840)
				break;
		if (f)
			fclose(f);
		uint32_t fb_prop = prop_id(plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", NULL, NULL);
		int flips = secs * hz, fails = 0;
		for (int i = 0; i < flips; i++) {
			drmModeAtomicReq *r = drmModeAtomicAlloc();
			drmModeAtomicAddProperty(r, plane_id, fb_prop, (i & 1) ? fb2 : fb_id);
			if (drmModeAtomicCommit(fd, r, 0, NULL))
				fails++;
			drmModeAtomicFree(r);
		}
		printf("flipped %d frames, %d failed\n", flips, fails);
		return 0;
	}
	if (secs)
		sleep(secs);
	return 0;
}
