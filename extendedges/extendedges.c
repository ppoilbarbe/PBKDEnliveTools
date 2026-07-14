/*
 * Extend edges - a frei0r filter for Kdenlive/MLT
 *
 * When a clip's geometry does not match the project profile, MLT pads it
 * with flat-colour letterbox/pillarbox bars before handing the frame to
 * filters. This effect detects those flat bars from the outer edges,
 * rebuilds them by stretching a sample of the adjacent picture content
 * across the bar, and blurs the result so the extension blends with the
 * real footage instead of showing a hard seam.
 *
 * Two parameters (both frei0r doubles, normalized 0..1, scaled to pixel
 * units by the Kdenlive effect XML via "factor"). Param names are single
 * tokens (no spaces) because MLT's command-line/property parser splits
 * "name=value" pairs on whitespace:
 *   0 "edge_width"   -> number of source pixels sampled from the picture
 *                        edge to build the stretched extension.
 *   1 "blur_amount"  -> blur radius (pixels) applied to the extended area.
 */

#include "frei0r.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_EDGE_PX 200.0
#define MAX_BLUR_PX 50.0
#define BAR_TOLERANCE 10
#define BAR_MATCH_TOLERANCE (BAR_TOLERANCE * 3)
#define MAX_BAR_FRACTION 0.45

typedef struct {
    unsigned int width;
    unsigned int height;
    double edge_width;
    double blur_amount;
} extendedges_instance_t;

int f0r_init(void)
{
    return 1;
}
void f0r_deinit(void) { }

void f0r_get_plugin_info(f0r_plugin_info_t* info)
{
    info->name = "Extend edges";
    info->author = "Marcel Spock";
    info->plugin_type = F0R_PLUGIN_TYPE_FILTER;
    info->color_model = F0R_COLOR_MODEL_RGBA8888;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 1;
    info->minor_version = 0;
    info->num_params = 2;
    info->explanation = "Fills letterbox/pillarbox bars left by a mismatched source geometry "
                        "with a stretched, blurred extension of the picture edges.";
}

void f0r_get_param_info(f0r_param_info_t* info, int param_index)
{
    switch (param_index) {
    case 0:
        info->name = "edge_width";
        info->type = F0R_PARAM_DOUBLE;
        info->explanation
            = "Number of source pixels sampled from the picture edge to build the extension";
        break;
    case 1:
        info->name = "blur_amount";
        info->type = F0R_PARAM_DOUBLE;
        info->explanation = "Blur radius applied to the extended (filled) region";
        break;
    default:
        info->name = "";
        info->type = F0R_PARAM_DOUBLE;
        info->explanation = "";
        break;
    }
}

f0r_instance_t f0r_construct(unsigned int width, unsigned int height)
{
    extendedges_instance_t* inst = calloc(1, sizeof(*inst));
    if (!inst)
        return 0;
    inst->width = width;
    inst->height = height;
    inst->edge_width = 20.0 / MAX_EDGE_PX;
    inst->blur_amount = 10.0 / MAX_BLUR_PX;
    return (f0r_instance_t)inst;
}

void f0r_destruct(f0r_instance_t instance)
{
    free(instance);
}

void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int param_index)
{
    extendedges_instance_t* inst = (extendedges_instance_t*)instance;
    double v = *((double*)param);
    if (v < 0.0)
        v = 0.0;
    if (v > 1.0)
        v = 1.0;
    switch (param_index) {
    case 0:
        inst->edge_width = v;
        break;
    case 1:
        inst->blur_amount = v;
        break;
    }
}

void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int param_index)
{
    const extendedges_instance_t* inst = (const extendedges_instance_t*)instance;
    double* v = (double*)param;
    switch (param_index) {
    case 0:
        *v = inst->edge_width;
        break;
    case 1:
        *v = inst->blur_amount;
        break;
    }
}

/* ---- bar detection ---------------------------------------------------- */

static int row_is_flat(const uint8_t* buf, unsigned width, unsigned y, uint8_t out_ref[3])
{
    const uint8_t* row = buf + (size_t)y * width * 4;
    uint8_t minc[3] = { 255, 255, 255 };
    uint8_t maxc[3] = { 0, 0, 0 };
    for (unsigned x = 0; x < width; x++) {
        for (int c = 0; c < 3; c++) {
            uint8_t v = row[x * 4 + c];
            if (v < minc[c])
                minc[c] = v;
            if (v > maxc[c])
                maxc[c] = v;
        }
    }
    for (int c = 0; c < 3; c++) {
        if ((int)maxc[c] - (int)minc[c] > BAR_TOLERANCE)
            return 0;
        out_ref[c] = row[c];
    }
    return 1;
}

static int col_is_flat(const uint8_t* buf, unsigned width, unsigned y0, unsigned y1, unsigned x,
                       uint8_t out_ref[3])
{
    uint8_t minc[3] = { 255, 255, 255 };
    uint8_t maxc[3] = { 0, 0, 0 };
    for (unsigned y = y0; y < y1; y++) {
        const uint8_t* px = buf + ((size_t)y * width + x) * 4;
        for (int c = 0; c < 3; c++) {
            uint8_t v = px[c];
            if (v < minc[c])
                minc[c] = v;
            if (v > maxc[c])
                maxc[c] = v;
        }
    }
    for (int c = 0; c < 3; c++) {
        if ((int)maxc[c] - (int)minc[c] > BAR_TOLERANCE)
            return 0;
    }
    const uint8_t* first = buf + ((size_t)y0 * width + x) * 4;
    out_ref[0] = first[0];
    out_ref[1] = first[1];
    out_ref[2] = first[2];
    return 1;
}

static int color_close(const uint8_t a[3], const uint8_t b[3])
{
    int d = abs((int)a[0] - (int)b[0]) + abs((int)a[1] - (int)b[1]) + abs((int)a[2] - (int)b[2]);
    return d <= BAR_MATCH_TOLERANCE;
}

static unsigned detect_bar_horizontal(const uint8_t* buf, unsigned width, unsigned height,
                                      int from_top)
{
    unsigned max_depth = (unsigned)(height * MAX_BAR_FRACTION);
    uint8_t ref[3];
    int have_ref = 0;
    unsigned depth = 0;
    for (unsigned i = 0; i < max_depth; i++) {
        unsigned y = from_top ? i : (height - 1 - i);
        uint8_t cur[3];
        if (!row_is_flat(buf, width, y, cur))
            break;
        if (!have_ref) {
            memcpy(ref, cur, 3);
            have_ref = 1;
        } else if (!color_close(cur, ref))
            break;
        depth++;
    }
    return depth;
}

static unsigned detect_bar_vertical(const uint8_t* buf, unsigned width, unsigned y0, unsigned y1,
                                    int from_left)
{
    unsigned band = y1 - y0;
    (void)band;
    unsigned max_depth = (unsigned)(width * MAX_BAR_FRACTION);
    uint8_t ref[3];
    int have_ref = 0;
    unsigned depth = 0;
    for (unsigned i = 0; i < max_depth; i++) {
        unsigned x = from_left ? i : (width - 1 - i);
        uint8_t cur[3];
        if (!col_is_flat(buf, width, y0, y1, x, cur))
            break;
        if (!have_ref) {
            memcpy(ref, cur, 3);
            have_ref = 1;
        } else if (!color_close(cur, ref))
            break;
        depth++;
    }
    return depth;
}

/* ---- stretch fill ------------------------------------------------------ */

static void lerp_pixel(const uint8_t* a, const uint8_t* b, double t, uint8_t* out)
{
    for (int c = 0; c < 4; c++) {
        out[c] = (uint8_t)(a[c] + (b[c] - a[c]) * t + 0.5);
    }
}

static void fill_top_bottom(uint8_t* buf, unsigned width, unsigned height, unsigned top_depth,
                            unsigned bottom_depth, double edge_width)
{
    unsigned content_h = height - top_depth - bottom_depth;
    if (content_h < 1)
        return;
    unsigned src_len = (unsigned)edge_width;
    if (src_len < 1)
        src_len = 1;
    if (src_len > content_h)
        src_len = content_h;

    for (unsigned x = 0; x < width; x++) {
        if (top_depth > 0) {
            unsigned content_start = top_depth;
            for (unsigned y = 0; y < top_depth; y++) {
                double frac = (top_depth <= 1)
                    ? 1.0
                    : (double)(top_depth - 1 - y) / (double)(top_depth - 1);
                double src_pos = content_start + frac * (src_len - 1);
                unsigned s0 = (unsigned)src_pos;
                unsigned s1 = (s0 + 1 < content_start + src_len) ? s0 + 1 : s0;
                double t = src_pos - s0;
                const uint8_t* pa = buf + ((size_t)s0 * width + x) * 4;
                const uint8_t* pb = buf + ((size_t)s1 * width + x) * 4;
                uint8_t out[4];
                lerp_pixel(pa, pb, t, out);
                memcpy(buf + ((size_t)y * width + x) * 4, out, 4);
            }
        }
        if (bottom_depth > 0) {
            unsigned content_end = height - bottom_depth - 1;
            unsigned bar_start = height - bottom_depth;
            for (unsigned y = bar_start; y < height; y++) {
                double frac = (bottom_depth <= 1)
                    ? 1.0
                    : (double)(y - bar_start) / (double)(bottom_depth - 1);
                double src_pos = content_end - frac * (src_len - 1);
                unsigned s0 = (unsigned)src_pos;
                unsigned s1 = (s0 + 1 <= content_end) ? s0 + 1 : s0;
                double t = src_pos - s0;
                const uint8_t* pa = buf + ((size_t)s0 * width + x) * 4;
                const uint8_t* pb = buf + ((size_t)s1 * width + x) * 4;
                uint8_t out[4];
                lerp_pixel(pa, pb, t, out);
                memcpy(buf + ((size_t)y * width + x) * 4, out, 4);
            }
        }
    }
}

static void fill_left_right(uint8_t* buf, unsigned width, unsigned height, unsigned left_depth,
                            unsigned right_depth, double edge_width)
{
    unsigned content_w = width - left_depth - right_depth;
    if (content_w < 1)
        return;
    unsigned src_len = (unsigned)edge_width;
    if (src_len < 1)
        src_len = 1;
    if (src_len > content_w)
        src_len = content_w;

    for (unsigned y = 0; y < height; y++) {
        uint8_t* row = buf + (size_t)y * width * 4;
        if (left_depth > 0) {
            unsigned content_start = left_depth;
            for (unsigned x = 0; x < left_depth; x++) {
                double frac = (left_depth <= 1)
                    ? 1.0
                    : (double)(left_depth - 1 - x) / (double)(left_depth - 1);
                double src_pos = content_start + frac * (src_len - 1);
                unsigned s0 = (unsigned)src_pos;
                unsigned s1 = (s0 + 1 < content_start + src_len) ? s0 + 1 : s0;
                double t = src_pos - s0;
                uint8_t out[4];
                lerp_pixel(row + s0 * 4, row + s1 * 4, t, out);
                memcpy(row + x * 4, out, 4);
            }
        }
        if (right_depth > 0) {
            unsigned content_end = width - right_depth - 1;
            unsigned bar_start = width - right_depth;
            for (unsigned x = bar_start; x < width; x++) {
                double frac = (right_depth <= 1)
                    ? 1.0
                    : (double)(x - bar_start) / (double)(right_depth - 1);
                double src_pos = content_end - frac * (src_len - 1);
                unsigned s0 = (unsigned)src_pos;
                unsigned s1 = (s0 + 1 <= content_end) ? s0 + 1 : s0;
                double t = src_pos - s0;
                uint8_t out[4];
                lerp_pixel(row + s0 * 4, row + s1 * 4, t, out);
                memcpy(row + x * 4, out, 4);
            }
        }
    }
}

/* ---- separable box blur (exact, via prefix sums) ----------------------- */

static void box_blur_h(const uint8_t* src, uint8_t* dst, unsigned w, unsigned h, int r)
{
    if (r <= 0) {
        memcpy(dst, src, (size_t)w * h * 4);
        return;
    }
    long* prefix = malloc((size_t)(w + 1) * sizeof(long));
    if (!prefix) {
        memcpy(dst, src, (size_t)w * h * 4);
        return;
    }
    for (unsigned y = 0; y < h; y++) {
        const uint8_t* row = src + (size_t)y * w * 4;
        uint8_t* orow = dst + (size_t)y * w * 4;
        for (int c = 0; c < 4; c++) {
            prefix[0] = 0;
            for (unsigned x = 0; x < w; x++)
                prefix[x + 1] = prefix[x] + row[x * 4 + c];
            for (unsigned x = 0; x < w; x++) {
                int xs = (int)x - r;
                if (xs < 0)
                    xs = 0;
                int xe = (int)x + r;
                if (xe >= (int)w)
                    xe = (int)w - 1;
                long sum = prefix[xe + 1] - prefix[xs];
                int count = xe - xs + 1;
                orow[x * 4 + c] = (uint8_t)(sum / count);
            }
        }
    }
    free(prefix);
}

static void box_blur_v(const uint8_t* src, uint8_t* dst, unsigned w, unsigned h, int r)
{
    if (r <= 0) {
        memcpy(dst, src, (size_t)w * h * 4);
        return;
    }
    long* prefix = malloc((size_t)(h + 1) * sizeof(long));
    if (!prefix) {
        memcpy(dst, src, (size_t)w * h * 4);
        return;
    }
    for (unsigned x = 0; x < w; x++) {
        for (int c = 0; c < 4; c++) {
            prefix[0] = 0;
            for (unsigned y = 0; y < h; y++)
                prefix[y + 1] = prefix[y] + src[((size_t)y * w + x) * 4 + c];
            for (unsigned y = 0; y < h; y++) {
                int ys = (int)y - r;
                if (ys < 0)
                    ys = 0;
                int ye = (int)y + r;
                if (ye >= (int)h)
                    ye = (int)h - 1;
                long sum = prefix[ye + 1] - prefix[ys];
                int count = ye - ys + 1;
                dst[((size_t)y * w + x) * 4 + c] = (uint8_t)(sum / count);
            }
        }
    }
    free(prefix);
}

/* ---- main update -------------------------------------------------------- */

void f0r_update(f0r_instance_t instance, double time, const uint32_t* inframe, uint32_t* outframe)
{
    (void)time;
    const extendedges_instance_t* inst = (const extendedges_instance_t*)instance;
    unsigned w = inst->width;
    unsigned h = inst->height;
    size_t bytes = (size_t)w * h * 4;

    memcpy(outframe, inframe, bytes);
    uint8_t* buf = (uint8_t*)outframe;

    unsigned top = detect_bar_horizontal(buf, w, h, 1);
    unsigned bottom = detect_bar_horizontal(buf, w, h, 0);
    if (top + bottom >= h) {
        top = 0;
        bottom = 0;
    }

    unsigned left = detect_bar_vertical(buf, w, top, h - bottom, 1);
    unsigned right = detect_bar_vertical(buf, w, top, h - bottom, 0);
    if (left + right >= w) {
        left = 0;
        right = 0;
    }

    if (top == 0 && bottom == 0 && left == 0 && right == 0)
        return;

    double edge_width_px = inst->edge_width * MAX_EDGE_PX;
    double blur_px = inst->blur_amount * MAX_BLUR_PX;
    int blur_r = (int)(blur_px + 0.5);

    if (top > 0 || bottom > 0)
        fill_top_bottom(buf, w, h, top, bottom, edge_width_px);
    if (left > 0 || right > 0)
        fill_left_right(buf, w, h, left, right, edge_width_px);

    if (blur_r > 0) {
        uint8_t* tmp = malloc(bytes);
        uint8_t* blurred = malloc(bytes);
        if (tmp && blurred) {
            box_blur_h(buf, tmp, w, h, blur_r);
            box_blur_v(tmp, blurred, w, h, blur_r);

            double feather = blur_r;
            for (unsigned y = 0; y < h; y++) {
                for (unsigned x = 0; x < w; x++) {
                    double a = 0.0;
                    if (top > 0 && y < top) {
                        double dist = top - y;
                        double v = dist <= feather ? dist / feather : 1.0;
                        if (v > a)
                            a = v;
                    }
                    if (bottom > 0 && y >= h - bottom) {
                        double dist = y - (h - bottom) + 1;
                        double v = dist <= feather ? dist / feather : 1.0;
                        if (v > a)
                            a = v;
                    }
                    if (left > 0 && x < left) {
                        double dist = left - x;
                        double v = dist <= feather ? dist / feather : 1.0;
                        if (v > a)
                            a = v;
                    }
                    if (right > 0 && x >= w - right) {
                        double dist = x - (w - right) + 1;
                        double v = dist <= feather ? dist / feather : 1.0;
                        if (v > a)
                            a = v;
                    }
                    if (a <= 0.0)
                        continue;
                    size_t idx = ((size_t)y * w + x) * 4;
                    for (int c = 0; c < 4; c++) {
                        double orig = buf[idx + c];
                        double bl = blurred[idx + c];
                        buf[idx + c] = (uint8_t)(orig + (bl - orig) * a + 0.5);
                    }
                }
            }
        }
        free(tmp);
        free(blurred);
    }
}

void f0r_update2(f0r_instance_t instance, double time, const uint32_t* inframe1,
                 const uint32_t* inframe2, const uint32_t* inframe3, uint32_t* outframe)
{
    (void)inframe2;
    (void)inframe3;
    f0r_update(instance, time, inframe1, outframe);
}
