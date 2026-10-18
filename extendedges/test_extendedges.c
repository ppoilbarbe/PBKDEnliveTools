/*
 * Standalone test harness for the extendedges frei0r plugin.
 *
 * Exercises f0r_update() directly (no MLT/frei0r host involved) on
 * synthetic frames, and checks that:
 *   - flat letterbox/pillarbox bars get filled with picture-like content,
 *   - when the picture has been moved, zoomed or rotated by an upstream
 *     transform (over a transparent background), the result is the
 *     untransformed result with the same transform applied: the picture
 *     and its extended edges move as one rectangle,
 *   - genuine picture content is left untouched,
 *   - a frame without any bar is left untouched.
 *
 * Conda-forge's mlt package ships without frei0r support, so this is the
 * reliable way to validate the plugin in the pixi environment.
 * Real end-to-end verification still requires a frei0r-enabled MLT/Kdenlive
 * (see README.md).
 */

#include "frei0r.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* 16:9 frame, 4:3 picture */
#define W 320u
#define H 180u
#define PIC_W 240.0
#define BAR 30u
#define PI 3.14159265358979323846

static int failures = 0;

static void check(int ok, const char* what)
{
    if (ok) {
        printf("OK: %s\n", what);
    } else {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static uint8_t* px(uint32_t* frame, unsigned x, unsigned y)
{
    return (uint8_t*)frame + ((size_t)y * W + x) * 4;
}

static void set_px(uint8_t* p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

/* Picture content: a checkerboard over a gradient, never close to black. */
static void set_picture(uint8_t* p, double u, double v)
{
    int chk = ((int)(u * 8) + (int)(v * 6)) & 1;
    set_px(p, chk ? 220 : (uint8_t)(40 + u * 180), (uint8_t)(40 + v * 200), chk ? 60 : 200, 255);
}

/* A rebuilt bar pixel must be opaque and no longer bar-coloured. */
static int is_filled(const uint8_t* p)
{
    return p[3] == 255 && (p[0] > 30 || p[1] > 30 || p[2] > 30);
}

static void run_filter(const uint32_t* in, uint32_t* out, double blur_px)
{
    f0r_instance_t inst = f0r_construct(W, H);
    if (!inst) {
        fprintf(stderr, "f0r_construct failed\n");
        exit(1);
    }
    double edge_width = 20.0 / 200.0;
    double blur_amount = blur_px / 50.0;
    f0r_set_param_value(inst, &edge_width, 0);
    f0r_set_param_value(inst, &blur_amount, 1);
    f0r_update(inst, 0.0, in, out);
    f0r_destruct(inst);
}

static int same_px(uint32_t* a, uint32_t* b, unsigned x, unsigned y)
{
    return memcmp(px(a, x, y), px(b, x, y), 4) == 0;
}

/* Maps a frame pixel centre through the inverse of a transform (rotation
 * 'angle' and zoom 'scale' around the frame centre, then shift (dx, dy)),
 * giving the matching position in the untransformed frame. */
static void untransform(double x, double y, double angle, double scale, double dx, double dy,
                        double* ox, double* oy)
{
    double rx = x - W / 2.0 - dx, ry = y - H / 2.0 - dy;
    double ca = cos(angle), sa = sin(angle);
    *ox = (ca * rx + sa * ry) / scale + W / 2.0;
    *oy = (-sa * rx + ca * ry) / scale + H / 2.0;
}

/* The 4:3 picture pillarboxed in the 16:9 frame, then transformed like
 * Kdenlive's transform effect does: over a transparent background, or
 * over the opaque pillarbox bars when untransformed. */
static void make_frame(uint32_t* in, double angle, double scale, double dx, double dy, int opaque)
{
    double x0 = (W - PIC_W) / 2.0;
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            double ox, oy;
            untransform(x + 0.5, y + 0.5, angle, scale, dx, dy, &ox, &oy);
            double u = (ox - x0) / PIC_W, v = oy / H;
            if (u >= 0.0 && u < 1.0 && v >= 0.0 && v < 1.0)
                set_picture(px(in, x, y), u, v);
            else
                set_px(px(in, x, y), 0, 0, 0, opaque ? 255 : 0);
        }
    }
}

static void test_letterbox(uint32_t* in, uint32_t* out)
{
    printf("-- letterbox\n");
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            if (y < BAR || y >= H - BAR)
                set_px(px(in, x, y), 16, 16, 16, 255);
            else
                set_picture(px(in, x, y), (double)x / W, (double)y / H);
        }
    }
    run_filter(in, out, 10.0);
    check(is_filled(px(out, W / 2, 2)), "top bar filled");
    check(is_filled(px(out, W / 2, H - 2)), "bottom bar filled");
    check(same_px(in, out, W / 2, H / 2), "content pixel untouched");
}

static void test_pillarbox(uint32_t* in, uint32_t* out)
{
    printf("-- pillarbox\n");
    make_frame(in, 0.0, 1.0, 0.0, 0.0, 1);
    run_filter(in, out, 10.0);
    check(is_filled(px(out, 1, H / 2)), "left bar filled");
    check(is_filled(px(out, W - 2, 1)), "right bar corner filled");
    check(same_px(in, out, W / 2, H / 2), "content pixel untouched");
}

/* Compares the filter output on a transformed frame with the untransformed
 * output transformed the same way (blur off: the box blur is axis-aligned,
 * so it can't be rotation invariant). */
static void test_transformed(uint32_t* in, uint32_t* out, uint32_t* ref, double degrees,
                             double scale, double dx, double dy)
{
    printf("-- picture rotated by %.0f degrees, zoom %.2f, shift (%.0f, %.0f)\n", degrees, scale,
           dx, dy);
    make_frame(in, 0.0, 1.0, 0.0, 0.0, 1);
    run_filter(in, ref, 0.0);
    double angle = degrees * PI / 180.0;
    make_frame(in, angle, scale, dx, dy, 0);
    run_filter(in, out, 0.0);

    double diff = 0.0;
    unsigned compared = 0, bad_alpha = 0, content_changed = 0;
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            double ox, oy;
            untransform(x + 0.5, y + 0.5, angle, scale, dx, dy, &ox, &oy);
            const uint8_t* p = px(out, x, y);
            if (px(in, x, y)[3] == 255 && !same_px(in, out, x, y))
                content_changed++;
            if (ox >= 2 && ox < W - 2 && oy >= 2 && oy < H - 2) {
                const uint8_t* q = px(ref, (unsigned)ox, (unsigned)oy);
                for (int c = 0; c < 3; c++)
                    diff += abs((int)p[c] - (int)q[c]);
                compared += 3;
                bad_alpha += p[3] != 255;
            } else if (ox < -2 || ox > W + 2 || oy < -2 || oy > H + 2) {
                bad_alpha += p[3] != 0;
            }
        }
    }
    diff /= compared;
    printf("  mean difference: %.2f\n", diff);
    check(diff < 2.0, "same as the untransformed result, transformed");
    check(bad_alpha == 0, "extended rectangle opaque, outside left transparent");
    check(content_changed == 0, "picture pixels untouched");
}

static void test_no_bars(uint32_t* in, uint32_t* out)
{
    printf("-- frame without bars\n");
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++)
            set_px(px(in, x, y), (uint8_t)(x * 7 + y * 3), (uint8_t)(y * 5), (uint8_t)(x ^ y), 255);
    }
    run_filter(in, out, 10.0);
    check(memcmp(in, out, (size_t)W * H * 4) == 0, "frame untouched");
}

int main(void)
{
    if (!f0r_init()) {
        fprintf(stderr, "f0r_init failed\n");
        return 1;
    }

    size_t n = (size_t)W * H;
    uint32_t* in = malloc(n * 4);
    uint32_t* out = malloc(n * 4);
    uint32_t* ref = malloc(n * 4);
    if (!in || !out || !ref) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    test_letterbox(in, out);
    test_pillarbox(in, out);
    test_transformed(in, out, ref, 0.0, 0.8, 20.0, -10.0);
    test_transformed(in, out, ref, 12.0, 1.0, 0.0, 0.0);
    test_transformed(in, out, ref, -25.0, 0.9, 0.0, 0.0);
    test_transformed(in, out, ref, 45.0, 1.0, 0.0, 0.0);
    test_transformed(in, out, ref, 70.0, 0.7, -30.0, 15.0);
    test_no_bars(in, out);

    free(in);
    free(out);
    free(ref);
    f0r_deinit();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("All checks passed\n");
    return 0;
}
