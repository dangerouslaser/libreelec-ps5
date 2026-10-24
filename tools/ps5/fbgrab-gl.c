// fbgrab-gl: dump the (possibly tiled) framebuffer on a plane by importing it
// as an EGLImage and reading it back with glReadPixels. Writes raw BGRX
// (XRGB8888 memory order) and prints the DP CRC like dvcrc.
// usage: fbgrab-gl PLANE_ID OUT.raw
// build: gcc -O2 -o fbgrab-gl fbgrab-gl.c -I/usr/include/libdrm -ldrm -lEGL -lGLESv2 -lgbm
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static uint16_t crc16_dp(uint16_t b, uint16_t d)
{
	for (int i = 15; i >= 0; i--) {
		int fb = ((b >> 15) ^ (d >> i)) & 1;
		b <<= 1;
		if (fb)
			b ^= 0x8005;
	}
	return b;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: fbgrab-gl PLANE_ID OUT.raw\n");
		return 1;
	}
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmModePlane *pl = drmModeGetPlane(fd, atoi(argv[1]));
	if (!pl || !pl->fb_id) {
		fprintf(stderr, "plane has no fb\n");
		return 1;
	}
	drmModeFB2 *fb = drmModeGetFB2(fd, pl->fb_id);
	int dfd;
	if (!fb || drmPrimeHandleToFD(fd, fb->handles[0], DRM_CLOEXEC, &dfd)) {
		perror("getfb2/prime");
		return 1;
	}
	printf("fb %ux%u %.4s modifier 0x%llx\n", fb->width, fb->height,
	       (char *)&fb->pixel_format, (unsigned long long)fb->modifier);

	int rfd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gbm = gbm_create_device(rfd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	eglInitialize(dpy, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "eglMakeCurrent failed 0x%x\n", eglGetError());
		return 1;
	}
	EGLint img_attr[] = {
		EGL_WIDTH, fb->width, EGL_HEIGHT, fb->height,
		EGL_LINUX_DRM_FOURCC_EXT, fb->pixel_format,
		EGL_DMA_BUF_PLANE0_FD_EXT, dfd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, fb->offsets[0],
		EGL_DMA_BUF_PLANE0_PITCH_EXT, fb->pitches[0],
		EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint)(fb->modifier & 0xffffffff),
		EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(fb->modifier >> 32),
		EGL_NONE
	};
	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = (void *)eglGetProcAddress("eglCreateImageKHR");
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	EGLImageKHR img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, img_attr);
	if (img == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "eglCreateImage failed 0x%x\n", eglGetError());
		return 1;
	}
	GLuint tex, fbo;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, img);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "fbo incomplete\n");
		return 1;
	}
	size_t n = (size_t)fb->width * fb->height;
	uint8_t *rgba = malloc(n * 4), *bgrx = malloc(n * 4);
	glReadPixels(0, 0, fb->width, fb->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	if (glGetError() != GL_NO_ERROR) {
		fprintf(stderr, "glReadPixels error\n");
		return 1;
	}
	/* GL rows are bottom-up relative to... for an imported image, row 0 is
	 * the first row in memory, which is the top scanline. */
	uint16_t crc[3] = { 0 };
	for (size_t i = 0; i < n; i++) {
		uint8_t r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
		bgrx[i * 4] = b; bgrx[i * 4 + 1] = g; bgrx[i * 4 + 2] = r; bgrx[i * 4 + 3] = 0xff;
		crc[0] = crc16_dp(crc[0], r << 8);
		crc[1] = crc16_dp(crc[1], g << 8);
		crc[2] = crc16_dp(crc[2], b << 8);
	}
	FILE *out = fopen(argv[2], "wb");
	fwrite(bgrx, 4, n, out);
	fclose(out);
	printf("fb crc: %04x %04x %04x\n", crc[0], crc[1], crc[2]);
	return 0;
}
