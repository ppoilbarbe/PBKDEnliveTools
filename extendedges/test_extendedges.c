/*
 * Standalone test harness for the extendedges frei0r plugin.
 *
 * Exercises f0r_update() directly (no MLT/frei0r host involved) on a
 * synthetic frame with black letterbox/pillarbox bars, and checks that:
 *   - the bars get filled with non-black content,
 *   - genuine picture content is left untouched.
 *
 * Conda-forge's mlt package ships without frei0r support, so this is the
 * reliable way to validate the plugin in the 'PBKDEnliveTools' conda env.
 * Real end-to-end verification still requires a frei0r-enabled MLT/Kdenlive
 * (see README.md).
 */

#include "frei0r.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define W 320u
#define H 240u
#define TOP_BAR 30u
#define BOTTOM_BAR 30u

static void fill_synthetic_frame(uint8_t* buf)
{
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            uint8_t* p = buf + ((size_t)y * W + x) * 4;
            if (y < TOP_BAR || y >= H - BOTTOM_BAR) {
                p[0] = 16;
                p[1] = 16;
                p[2] = 16;
                p[3] = 255;
            } else {
                p[0] = (uint8_t)((x * 255) / W);
                p[1] = (uint8_t)((y * 255) / H);
                p[2] = 128;
                p[3] = 255;
            }
        }
    }
}

int main(void)
{
    if (!f0r_init()) {
        fprintf(stderr, "f0r_init failed\n");
        return 1;
    }

    f0r_instance_t inst = f0r_construct(W, H);
    if (!inst) {
        fprintf(stderr, "f0r_construct failed\n");
        return 1;
    }

    double edge_width = 20.0 / 200.0;
    double blur_amount = 10.0 / 50.0;
    f0r_set_param_value(inst, &edge_width, 0);
    f0r_set_param_value(inst, &blur_amount, 1);

    size_t n = (size_t)W * H;
    uint32_t* in = malloc(n * 4);
    uint32_t* out = malloc(n * 4);
    if (!in || !out) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    fill_synthetic_frame((uint8_t*)in);

    f0r_update(inst, 0.0, in, out);

    uint8_t* obuf = (uint8_t*)out;
    int failures = 0;

    /* The outer edge of the top bar should no longer be flat black. */
    const uint8_t* top_px = obuf + ((size_t)2 * W + (W / 2)) * 4;
    if (top_px[0] <= 20 && top_px[1] <= 20 && top_px[2] <= 20) {
        fprintf(stderr, "FAIL: top bar pixel still black (%d,%d,%d)\n", top_px[0], top_px[1],
                top_px[2]);
        failures++;
    } else {
        printf("OK: top bar filled (%d,%d,%d)\n", top_px[0], top_px[1], top_px[2]);
    }

    /* The outer edge of the bottom bar should no longer be flat black. */
    const uint8_t* bottom_px = obuf + ((size_t)(H - 2) * W + (W / 2)) * 4;
    if (bottom_px[0] <= 20 && bottom_px[1] <= 20 && bottom_px[2] <= 20) {
        fprintf(stderr, "FAIL: bottom bar pixel still black (%d,%d,%d)\n", bottom_px[0],
                bottom_px[1], bottom_px[2]);
        failures++;
    } else {
        printf("OK: bottom bar filled (%d,%d,%d)\n", bottom_px[0], bottom_px[1], bottom_px[2]);
    }

    /* Genuine content, far from any bar, must be untouched. */
    unsigned cy = H / 2, cx = W / 2;
    const uint8_t* content_out = obuf + ((size_t)cy * W + cx) * 4;
    const uint8_t* content_in = (const uint8_t*)in + ((size_t)cy * W + cx) * 4;
    if (memcmp(content_out, content_in, 4) != 0) {
        fprintf(stderr, "FAIL: content pixel altered: in=(%d,%d,%d) out=(%d,%d,%d)\n",
                content_in[0], content_in[1], content_in[2], content_out[0], content_out[1],
                content_out[2]);
        failures++;
    } else {
        printf("OK: content pixel untouched\n");
    }

    f0r_destruct(inst);
    free(in);
    free(out);
    f0r_deinit();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("All checks passed\n");
    return 0;
}
