#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <unistd.h>
#include <pwd.h>
#include <pthread.h>

#if defined(__GCW0_DRM__)
#ifndef GL_ES_VERSION_2_0
#include <GLES2/gl2.h>
#endif
#include <GLES2/gl2ext.h>
#endif
#include <EGL/egl.h>
#if defined(__GCW0_DRM__)
#include <EGL/eglext.h>

#include <gbm.h>
#include <libdrm/drm_fourcc.h>

#include <libdrm/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

#include <fcntl.h>
#include <linux/input.h>
#include <alsa/asoundlib.h>

#include "game.h"

#define WND_TITLE    "OpenLara"

// timing
unsigned int startTime;

int osGetTimeMS() {
    timeval t;
    gettimeofday(&t, NULL);
    return int((t.tv_sec - startTime) * 1000 + t.tv_usec / 1000);
}

// sound
snd_pcm_uframes_t   SND_FRAMES = 512;
snd_pcm_t           *sndOut;
Sound::Frame        *sndData;
pthread_t           sndThread;

void* sndFill(void *arg) {
    while (sndOut) {
        Sound::fill(sndData, SND_FRAMES);

        int count = SND_FRAMES;
        while (count > 0) {
            int frames = snd_pcm_writei(sndOut, &sndData[SND_FRAMES - count], count);
            if (frames < 0) {
                frames = snd_pcm_recover(sndOut, frames, 0);
                if (frames == -EAGAIN) {
                    LOG("snd_pcm_writei try again\n");
                    sleep(1);
                    continue;
                }
                if (frames < 0) {
                    LOG("snd_pcm_writei failed: %s\n", snd_strerror(frames));
                    sndOut = NULL;
                    return NULL;
                }
            }
            count -= frames;
        }
        
        snd_pcm_prepare(sndOut);
    }
    return NULL;
}

bool sndInit() {
    unsigned int freq = 44100;

    int err;
    
    // In the perfect world ReedPlayer-Clover process 
    // will release ALSA device before app running, but...
    for (int i = 0; i < 20; i++) { // 20 * 0.1 = 2 secs
        sndOut = NULL;
        if ((err = snd_pcm_open(&sndOut, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
            LOG("sound: try to snd_pcm_open #%d...\n", i);
            usleep(100000); // wait for 100 ms
            continue;
        }
        break;
    }
    
    // I've bad news for you
    if (!sndOut) {
        LOG("! sound: snd_pcm_open %s\n", snd_strerror(err));
        return false;
    }

    snd_pcm_hw_params_t *params;

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(sndOut, params);
    snd_pcm_hw_params_set_access(sndOut, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    snd_pcm_hw_params_set_channels(sndOut, params, 2);
    snd_pcm_hw_params_set_format(sndOut, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(sndOut, params, &freq, NULL);

    snd_pcm_hw_params_set_periods(sndOut, params, 4, 0);
    snd_pcm_hw_params_set_period_size_near(sndOut, params, &SND_FRAMES, NULL);
    snd_pcm_hw_params_get_period_size(params, &SND_FRAMES, 0);

    snd_pcm_hw_params(sndOut, params);
    snd_pcm_prepare(sndOut);

    sndData = new Sound::Frame[SND_FRAMES];
    memset(sndData, 0, SND_FRAMES * sizeof(Sound::Frame));
    if ((err = snd_pcm_writei(sndOut, sndData, SND_FRAMES)) < 0) {
        LOG("! sound: write %s\n", snd_strerror(err));
        sndOut = NULL;
    }

    snd_pcm_start(sndOut);
    pthread_create(&sndThread, NULL, sndFill, NULL);

    return true;
}

void sndFree() {
    pthread_cancel(sndThread);
    snd_pcm_drop(sndOut);
    snd_pcm_drain(sndOut);
    snd_pcm_close(sndOut);
    delete[] sndData;
}

#if defined(__GCW0_DRM__)
struct drm {
    int fd;

    drmModeModeInfo *mode;
    uint32_t crtc_id;
    uint32_t connector_id;
} drm;

struct drm_fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
};

struct gbm {
    struct gbm_device *dev;
    struct gbm_surface *surface;
    uint32_t format;
    int width, height;
} gbm;

struct gbm_bo *bo;

static void page_flip_handler(int fd, unsigned int frame,
                  unsigned int sec, unsigned int usec, void *data)
{
    /* suppress 'unused parameter' warnings */
    (void)fd, (void)frame, (void)sec, (void)usec;

    int *waiting_for_flip = data;
    *waiting_for_flip = 0;
}

drmEventContext evctx = {
    .version = 2,
    .page_flip_handler = page_flip_handler,
};

static int get_resources(int fd, drmModeRes **resources)
{
    *resources = drmModeGetResources(fd);
    if (*resources == NULL)
        return -1;
    return 0;
}

static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
                                    const drmModeEncoder *encoder) {
    int i;

    for (i = 0; i < resources->count_crtcs; i++) {
        /* possible_crtcs is a bitmask as described here:
         * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
         */
        const uint32_t crtc_mask = 1 << i;
        const uint32_t crtc_id = resources->crtcs[i];
        if (encoder->possible_crtcs & crtc_mask) {
            return crtc_id;
        }
    }

    /* no match found */
    return -1;
}

static uint32_t find_crtc_for_connector(const struct drm *drm, const drmModeRes *resources,
                                        const drmModeConnector *connector) {
    int i;

    for (i = 0; i < connector->count_encoders; i++) {
        const uint32_t encoder_id = connector->encoders[i];
        drmModeEncoder *encoder = drmModeGetEncoder(drm->fd, encoder_id);

        if (encoder) {
            const uint32_t crtc_id = find_crtc_for_encoder(resources, encoder);

            drmModeFreeEncoder(encoder);
            if (crtc_id != 0) {
                return crtc_id;
            }
        }
    }

    /* no match found */
    return -1;
}

#define MAX_DRM_DEVICES 64

static int find_drm_device(drmModeRes **resources)
{
    drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
    int num_devices, fd = -1;

    num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
    if (num_devices < 0) {
        printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
        return -1;
    }

    for (int i = 0; i < num_devices; i++) {
        drmDevicePtr device = devices[i];
        int ret;

        if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY)))
            continue;
        /* OK, it's a primary device. If we can get the
         * drmModeResources, it means it's also a
         * KMS-capable device.
         */
        fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
        if (fd < 0)
            continue;
        ret = get_resources(fd, resources);
        if (!ret)
            break;
        close(fd);
        fd = -1;
    }
    drmFreeDevices(devices, num_devices);

    if (fd < 0)
        printf("no drm device found!\n");
    return fd;
}

static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb *fb = data;

    if (fb->fb_id)
        drmModeRmFB(drm_fd, fb->fb_id);

    free(fb);
}

struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb *fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, format,
         strides[4] = {0}, handles[4] = {0},
         offsets[4] = {0}, flags = 0;
    int ret = -1;

    if (fb)
        return fb;

    fb = calloc(1, sizeof *fb);
    fb->bo = bo;

    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    format = gbm_bo_get_format(bo);

    if (gbm_bo_get_modifier && gbm_bo_get_plane_count &&
        gbm_bo_get_stride_for_plane && gbm_bo_get_offset) {

        uint64_t modifiers[4] = {0};
        modifiers[0] = gbm_bo_get_modifier(bo);
        const int num_planes = gbm_bo_get_plane_count(bo);
        for (int i = 0; i < num_planes; i++) {
            strides[i] = gbm_bo_get_stride_for_plane(bo, i);
            handles[i] = gbm_bo_get_handle(bo).u32;
            offsets[i] = gbm_bo_get_offset(bo, i);
            modifiers[i] = modifiers[0];
        }

        if (modifiers[0]) {
            flags = DRM_MODE_FB_MODIFIERS;
            printf("Using modifier %i \n", modifiers[0]);
        }

        ret = drmModeAddFB2WithModifiers(drm_fd, width, height,
                                format, handles, strides, offsets,
                                modifiers, &fb->fb_id, flags);
    }

    if (ret) {
        if (flags)
            fprintf(stderr, "Modifiers failed!\n");

        uint32_t tmp_handles[4] = {gbm_bo_get_handle(bo).u32,0,0,0};
        uint32_t tmp_strides[4] = {gbm_bo_get_stride(bo),0,0,0};
        memcpy(handles, tmp_handles, 16);
        memcpy(strides, tmp_strides, 16);
        memset(offsets, 0, 16);
        ret = drmModeAddFB2(drm_fd, width, height, format,
                            handles, strides, offsets, &fb->fb_id, 0);
    }

    if (ret) {
        printf("failed to create fb: %s\n", strerror(errno));
        free(fb);
        return NULL;
    }

    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

    return fb;
}

int init_drm(struct drm *drm, unsigned int vrefresh)
{
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    int i, ret, area;

    drm->fd = find_drm_device(&resources);
    if (drm->fd < 0) {
        printf("could not open drm device\n");
        return -1;
    }

    if (!resources) {
        printf("drmModeGetResources failed: %s\n", strerror(errno));
        return -1;
    }

    /* find a connected connector: */
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm->fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            /* it's connected, let's use this! */
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    if (!connector) {
        /* we could be fancy and listen for hotplug events and wait for
         * a connector..
         */
        printf("no connected connector!\n");
        return -1;
    }

    /* find preferred mode or the highest resolution mode: */
    if (!drm->mode) {
        for (i = 0, area = 0; i < connector->count_modes; i++) {
            drmModeModeInfo *current_mode = &connector->modes[i];

            if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
                drm->mode = current_mode;
                break;
            }

            int current_area = current_mode->hdisplay * current_mode->vdisplay;
            if (current_area > area) {
                drm->mode = current_mode;
                area = current_area;
            }
        }
    }

    if (!drm->mode) {
        printf("could not find mode!\n");
        return -1;
    }

    /* find encoder: */
    for (i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
        if (encoder->encoder_id == connector->encoder_id)
            break;
        drmModeFreeEncoder(encoder);
        encoder = NULL;
    }

    if (encoder) {
        drm->crtc_id = encoder->crtc_id;
    } else {
        uint32_t crtc_id = find_crtc_for_connector(drm, resources, connector);
        if (crtc_id == 0) {
            printf("no crtc found!\n");
            return -1;
        }

        drm->crtc_id = crtc_id;
    }

    drmModeFreeResources(resources);

    drm->connector_id = connector->connector_id;

    return 0;
}

static int drm_run(const struct gbm *gbm)
{
    int waiting_for_flip = 1;
    struct gbm_bo *next_bo;
    struct drm_fb *drm_fb;

    next_bo = gbm_surface_lock_front_buffer(gbm->surface);
    drm_fb = drm_fb_get_from_bo(next_bo);
    if (!drm_fb) {
        LOG("Failed to get a new framebuffer BO\n");
        return -1;
    }

    int ret = drmModePageFlip(drm.fd, drm.crtc_id, drm_fb->fb_id,
                                DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
    if (ret) {
        LOG("failed to queue page flip: %s\n", strerror(errno));
        return -1;
    }
    while (waiting_for_flip) {
        drmHandleEvent(drm.fd, &evctx);
    }

    // release last buffer to render on again
    gbm_surface_release_buffer(gbm->surface, bo);
    bo = next_bo;

    return 0;
}

#define WEAK __attribute__((weak))

WEAK struct gbm_surface *
gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                  uint32_t width, uint32_t height,
                                  uint32_t format,
                                  const uint64_t *modifiers,
                                  const unsigned int count);

const struct gbm * init_gbm(int drm_fd, int w, int h, uint32_t format, uint64_t modifier)
{
    gbm.dev = gbm_create_device(drm_fd);
    gbm.format = format;
    gbm.surface = NULL;

    if (gbm_surface_create_with_modifiers) {
        gbm.surface = gbm_surface_create_with_modifiers(gbm.dev, w, h,
                                                        gbm.format,
                                                        &modifier, 1);

    }

    if (!gbm.surface) {
        if (modifier != DRM_FORMAT_MOD_LINEAR) {
            fprintf(stderr, "Modifiers requested but support isn't available\n");
            return NULL;
        }
        gbm.surface = gbm_surface_create(gbm.dev, w, h,
                                            gbm.format,
                                            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

    }

    if (!gbm.surface) {
        printf("failed to create gbm surface\n");
        return NULL;
    }

    gbm.width = w;
    gbm.height = h;

    return &gbm;
}

static int
match_config_to_visual(EGLDisplay egl_display,
                       EGLint visual_id,
                       EGLConfig *configs,
                       int count)
{
    int i;

    for (i = 0; i < count; ++i) {
        EGLint id;

        if (!eglGetConfigAttrib(egl_display,
                                configs[i], EGL_NATIVE_VISUAL_ID,
                                &id))
            continue;

        if (id == visual_id)
            return i;
    }

    return -1;
}

static bool
egl_choose_config(EGLDisplay egl_display, const EGLint *attribs,
                  EGLint visual_id, EGLConfig *config_out)
{
    EGLint count = 0;
    EGLint matched = 0;
    EGLConfig *configs;
    int config_index = -1;

    if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1) {
        LOG("No EGL configs to choose from.\n");
        return false;
    }
    configs = malloc(count * sizeof *configs);
    if (!configs)
        return false;

    if (!eglChooseConfig(egl_display, attribs, configs,
                          count, &matched) || !matched) {
        LOG("No EGL configs with appropriate attributes.\n");
        goto out;
    }

    if (!visual_id)
        config_index = 0;

    if (config_index == -1)
        config_index = match_config_to_visual(egl_display,
                                                      visual_id,
                                                      configs,
                                                      matched);

    if (config_index != -1)
        *config_out = configs[config_index];

out:
    free(configs);
    if (config_index == -1)
        return false;

    return true;
}
#endif

// Window
struct FrameBuffer {
    unsigned short width;
    unsigned short height;
} fb;

EGLDisplay display;
EGLSurface surface;
EGLContext context;

bool eglInit() {
    LOG("EGL init context...\n");

#if defined(__GCW0_DRM__)
    unsigned int vrefresh = 0;
    EGLint major, minor;
    const char *egl_exts_client, *egl_exts_dpy, *gl_exts;

    struct gbm *pgbm = &gbm;
    struct drm *pdrm = &drm;
    
    struct drm_fb *drm_fb;
    
    EGLConfig config;
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
#else
    fb_var_screeninfo vinfo;
    int fd = open("/dev/fb0", O_RDWR);
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        LOG("! can't get framebuffer size\n");
        return false;
    }
    close(fd);

    fb.width  = vinfo.xres;
    fb.height = vinfo.yres;

#endif
    const EGLint eglAttr[] = {
        EGL_RENDERABLE_TYPE,        EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,           EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,              5,
        EGL_GREEN_SIZE,             6,
        EGL_RED_SIZE,               5,
        EGL_DEPTH_SIZE,             16,
        EGL_NONE
    };

    const EGLint ctxAttr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
#if defined(__GCW0_DRM__)
    init_drm(&drm, vrefresh);
    if (!pdrm->fd) {
        LOG("failed to initialize DRM\n");
        return false;
    }
    
    fb.width  = pdrm->mode->hdisplay;
    fb.height = pdrm->mode->vdisplay;

    pgbm = init_gbm(pdrm->fd, pdrm->mode->hdisplay, pdrm->mode->vdisplay,
                        DRM_FORMAT_RGB565, DRM_FORMAT_MOD_LINEAR);
    if (!pgbm) {
        LOG("failed to initialize GBM\n");
        return false;
    }

    eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (eglGetPlatformDisplayEXT) {
        display = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, pgbm->dev, NULL);
    }
    if (display == EGL_NO_DISPLAY) {
        LOG("eglGetDisplay = EGL_NO_DISPLAY\n");
        return false;
    }

    if (!eglInitialize(display, &major, &minor)) {
        LOG("eglInitialize = EGL_FALSE\n");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG("failed to bind api EGL_OPENGL_ES_API\n");
        return false;
    }

    if (!egl_choose_config(display, eglAttr, pgbm->format, &config)) {
        LOG("failed to choose config\n");
        return false;
    }

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttr);
    if (context == EGL_NO_CONTEXT) {
        LOG("eglCreateContext = EGL_NO_CONTEXT\n");
        return false;
    }

    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)pgbm->surface, NULL);
    if (surface == EGL_NO_SURFACE) {
        LOG("eglCreateWindowSurface = EGL_NO_SURFACE\n");
        return false;
    }

    // connect the context to the surface
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOG("eglMakeCurrent = EGL_FALSE\n");
        return false;
    }

    eglSwapBuffers(display, surface);
    bo = gbm_surface_lock_front_buffer(pgbm->surface);
    drm_fb = drm_fb_get_from_bo(bo);
    if (!drm_fb) {
        LOG("Failed to get a new framebuffer BO\n");
        return false;
    }

    // set mode
    int ret = drmModeSetCrtc(pdrm->fd, pdrm->crtc_id, drm_fb->fb_id, 0, 0,
                              &pdrm->connector_id, 1, pdrm->mode);
    if (ret) {
        LOG("failed to set mode: %s\n", strerror(errno));
        return false;
    }
    
    // Print EGL and OpenGL info
    egl_exts_client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    egl_exts_dpy = eglQueryString(display, EGL_EXTENSIONS);
    printf("Using display %p with EGL version %d.%d\n",
            display, major, minor);

    printf("===================================\n");
    printf("EGL information:\n");
    printf("  version: \"%s\"\n", eglQueryString(display, EGL_VERSION));
    printf("  vendor: \"%s\"\n", eglQueryString(display, EGL_VENDOR));
    printf("  client extensions: \"%s\"\n", egl_exts_client);
    printf("  display extensions: \"%s\"\n", egl_exts_dpy);
    printf("===================================\n");

    gl_exts = (char *)glGetString(GL_EXTENSIONS);
    printf("OpenGL ES 2.x information:\n");
    printf("  version: \"%s\"\n", glGetString(GL_VERSION));
    printf("  shading language version: \"%s\"\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("  vendor: \"%s\"\n", glGetString(GL_VENDOR));
    printf("  renderer: \"%s\"\n", glGetString(GL_RENDERER));
    printf("  extensions: \"%s\"\n", gl_exts);
    printf("===================================\n");
#else
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOG("eglGetDisplay = EGL_NO_DISPLAY\n");
        return false;
    }

    if (eglInitialize(display, NULL, NULL) == EGL_FALSE) {
        LOG("eglInitialize = EGL_FALSE\n");
        return false;
    }

    EGLConfig config;
    EGLint configCount;

    if (eglChooseConfig(display, eglAttr, &config, 1, &configCount) == EGL_FALSE || configCount == 0) {
        LOG("eglChooseConfig = EGL_FALSE\n");
        return false;
    }

    surface = eglCreateWindowSurface(display, config, 0, NULL);
    if (surface == EGL_NO_SURFACE) {
        LOG("eglCreateWindowSurface = EGL_NO_SURFACE\n");
        return false;
    }

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttr);
    if (context == EGL_NO_CONTEXT) {
        LOG("eglCreateContext = EGL_NO_CONTEXT\n");
        return false;
    }
    
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOG("eglMakeCurrent = EGL_FALSE\n");
        return false;
    }
#endif

    return true;
}

void eglFree() {
    LOG("EGL release context\n");
#if !defined(__GCW0_DRM__)
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
#endif
    eglTerminate(display);
}

// Input
int ev_buttons;
int ev_haptic;
ff_effect joy_ff;

#define JOY_RUMBLE_TIMER  50
#define JOY_RUMBLE_GAIN   0xFFFF

vec2 joyL, joyR;
float joyVGain, joyVGainOld;
int   joyVTime;

bool osJoyReady(int index) {
    return index == 0;
}

void osJoyVibrate(int index, float L, float R) {
    if (ev_haptic == -1) return;
    joyVGain = (L + R) * 0.5f;
}

float joyAxisValue(int value) {
    return value / 1536.0f - 1.0f;
}

vec2 joyDir(const vec2 &value) {
    float dist = min(1.0f, value.length());
    return value.normal() * dist;
}

void joyRumble() {
    if (joy_ff.id == -1 || joyVGain == joyVGainOld || osGetTimeMS() < joyVTime)
        return;

    joy_ff.u.rumble.strong_magnitude = int(JOY_RUMBLE_GAIN * joyVGain);
    ioctl(ev_haptic, EVIOCSFF, &joy_ff);

    joyVGainOld = joyVGain;
    joyVTime    = osGetTimeMS() + JOY_RUMBLE_TIMER;
}

JoyKey codeToJoyKey(int code) {
    switch (code) {
    // gamepad
        case KEY_LEFT       : return jkLeft;
        case KEY_RIGHT      : return jkRight;
        case KEY_UP         : return jkUp;
        case KEY_DOWN       : return jkDown;
        case KEY_LEFTCTRL   : return jkB;
        case KEY_LEFTALT    : return jkA;
        case KEY_SPACE      : return jkY;
        case KEY_LEFTSHIFT  : return jkX;
        case KEY_TAB        : return jkLB;
        case KEY_BACKSPACE  : return jkRB;
        case KEY_ESC        : return jkSelect;
        case KEY_ENTER      : return jkStart;
        case KEY_KPSLASH    : return jkL;
        case KEY_KPDOT      : return jkR;
        case KEY_PAGEUP     : return jkLT;
        case KEY_PAGEDOWN   : return jkRT;
        case KEY_POWER      : {
            Game::quickSave();
            Core::quit();
        }
    }
    return jkNone;
}

void inputInit() {
    joyL = joyR = vec2(0.0f);
    joyVGain    = 0.0f;
    joyVGainOld = 0.0f;
    joyVTime    = osGetTimeMS();

    memset(&joy_ff, 0, sizeof(joy_ff));
    joy_ff.id = -1;

    // TODO find compatible device instead of hardcode
    ev_buttons = open("/dev/input/event3", O_NONBLOCK | O_RDONLY);
    ev_haptic  = open("/dev/input/event1", O_RDWR);

    if (ev_buttons == -1) {
        LOG("! input device was not found\n");
    }

    if (ev_haptic == -1) {
        LOG("! haptic device was not found\n");
    } else {
        joy_ff.type                      = FF_RUMBLE;
        joy_ff.id                        = -1;
        joy_ff.replay.length             = 0;
        joy_ff.replay.delay              = 0;
        joy_ff.u.rumble.strong_magnitude = 0;
        joy_ff.u.rumble.weak_magnitude   = 0;

        if (ioctl(ev_haptic, EVIOCSFF, &joy_ff) != -1) {
            input_event gain;
            gain.type  = EV_FF;
            gain.code  = FF_GAIN;
            gain.value = JOY_RUMBLE_GAIN;
            write(ev_haptic, &gain, sizeof(gain));

            input_event state;
            state.type  = EV_FF;
            state.code  = joy_ff.id;
            state.value = 1; // play
            write(ev_haptic, &state, sizeof(state));
        } else {
            LOG("! can't initialize vibration\n");
            close(ev_haptic);
            ev_haptic = -1;
        }
    }
}

void inputFree() {
    if (ev_buttons != -1) close(ev_buttons);
    if (ev_haptic  != -1) close(ev_haptic);
}

void inputUpdate() {
    joyRumble();

    if (ev_buttons == -1) return;

    input_event events[64];

    int rb = read(ev_buttons, events, sizeof(events));

    input_event *e = events;
    while (rb > 0) {
        switch (e->type) {
            case EV_KEY : {
                JoyKey key = codeToJoyKey(e->code);
                Input::setJoyDown(0, key, e->value != 0);
                break;
            }
            case EV_ABS : {
                switch (e->code) {
                // Left stick
                    case ABS_X  : joyL.x = -joyAxisValue(e->value); break;
                    case ABS_Y  : joyL.y = -joyAxisValue(e->value); break;
                // Right stick
                    case ABS_RX : joyR.x = joyAxisValue(e->value); break;
                    case ABS_RY : joyR.y = joyAxisValue(e->value); break;
                }

                Input::setJoyPos(0, jkL, joyDir(joyL));
                Input::setJoyPos(0, jkR, joyDir(joyR));
            }
        }
        e++;
        rb -= sizeof(events[0]);
    }
}

int main(int argc, char **argv) {
    if (!eglInit()) {
        LOG("! can't initialize EGL context\n");
        return -1;
    }
       
    Core::width  = fb.width;
    Core::height = fb.height;

    cacheDir[0] = saveDir[0] = contentDir[0] = 0;

    const char *home;
    if (!(home = getenv("HOME")))
        home = getpwuid(getuid())->pw_dir;
    strcpy(contentDir, home);
    strcat(contentDir, "/.openlara/");

    struct stat st = {0};

    if (stat(contentDir, &st) == -1) {
        LOG("no data directory found, please copy the original game content into %s\n", contentDir);
        return -1;
    }

    strcpy(saveDir, contentDir);

    strcpy(cacheDir, contentDir);
    strcat(cacheDir, "cache/");

    if (stat(cacheDir, &st) == -1 && mkdir(cacheDir, 0777) == -1) {
        cacheDir[0] = 0;
        LOG("can't create /home/.openlara/cache/\n");
    }

    timeval t;
    gettimeofday(&t, NULL);
    startTime = t.tv_sec;

    Game::init();

    Game::quickLoad(true);

    inputInit();
    sndInit();

    while (!Core::isQuit) {
        inputUpdate();

        if (Game::update()) {
            Game::render();
            Core::waitVBlank();
            eglSwapBuffers(display, surface);
#if defined(__GCW0_DRM__)
	    drm_run(&gbm);
#endif
        } else
            usleep(9000);
    };

    inputFree();

    Game::deinit();
    eglFree();

    sndFree();

    return 0;
}
