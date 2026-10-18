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
 * The picture may also have been moved, zoomed or rotated by a transform
 * placed before this effect (possibly keyframed), leaving it over a
 * transparent background. Its rotated rectangle is then measured on every
 * frame, and the extension fills the rectangle the whole frame would cover
 * under the same transform: the picture with its extended edges moves and
 * rotates as one, exactly like the untransformed result.
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_EDGE_PX 200.0
#define MAX_BLUR_PX 50.0
#define BAR_TOLERANCE 10
#define MAX_BAR_FRACTION 0.45
#define MIN_BORDER_BAR_FRACTION 0.10
#define MAX_BAR_AREA_FRACTION 0.75
/* Fewer boundary points than this on a side: the side is off-frame. */
#define MIN_SIDE_POINTS 8
/* Rotation (radians) under which the picture is considered upright. */
#define AXIS_ALIGNED_EPS 0.01
/* Stretched samples are taken this far (px) inside the picture edge, away
 * from its antialiased border. */
#define EDGE_INSET 1.0

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
    info->minor_version = 1;
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

/* ---- bar detection ----------------------------------------------------
 *
 * Two kinds of "bars" are handled:
 *   - opaque flat-colour bars, as MLT pads a clip whose geometry doesn't
 *     match the profile;
 *   - fully transparent areas, as Kdenlive's transform effects leave
 *     around a moved, zoomed or rotated picture.
 * Detection is done per scanline: in every row and every column, the run
 * of bar pixels starting at each frame border is marked as bar.
 */

static int px_is_bar(const uint8_t* px, const uint8_t ref[4])
{
    if (ref[3] == 0)
        return px[3] == 0;
    for (int c = 0; c < 4; c++) {
        if (abs((int)px[c] - (int)ref[c]) > BAR_TOLERANCE)
            return 0;
    }
    return 1;
}

/* Picks the bar reference colour from the frame corners (at least two of
 * them must agree), then checks that enough of the frame border actually
 * has that colour. Returns 0 when the frame shows no bar. */
static int pick_bar_ref(const uint8_t* buf, unsigned w, unsigned h, uint8_t ref[4])
{
    const uint8_t* corners[4] = {
        buf,
        buf + (size_t)(w - 1) * 4,
        buf + (size_t)(h - 1) * w * 4,
        buf + ((size_t)(h - 1) * w + (w - 1)) * 4,
    };
    int best = -1, best_count = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t cand[4];
        memcpy(cand, corners[i], 4);
        int count = 0;
        for (int j = 0; j < 4; j++)
            count += px_is_bar(corners[j], cand);
        if (count > best_count) {
            best_count = count;
            best = i;
        }
    }
    if (best_count < 2)
        return 0;
    memcpy(ref, corners[best], 4);

    size_t border = 0, matching = 0;
    for (unsigned x = 0; x < w; x++) {
        matching += px_is_bar(buf + (size_t)x * 4, ref);
        matching += px_is_bar(buf + ((size_t)(h - 1) * w + x) * 4, ref);
        border += 2;
    }
    for (unsigned y = 1; y + 1 < h; y++) {
        matching += px_is_bar(buf + (size_t)y * w * 4, ref);
        matching += px_is_bar(buf + ((size_t)y * w + (w - 1)) * 4, ref);
        border += 2;
    }
    return matching >= border * MIN_BORDER_BAR_FRACTION;
}

/* Marks in 'mask' the run of bar pixels starting at each end of one line
 * (a row or a column, described by its first pixel index and its stride).
 * A line made only of bar pixels is marked entirely. Partial runs deeper
 * than 'max_depth' are ignored: with opaque bars they are most likely dark
 * picture content, and genuine bars are caught by the other direction. */
static void mark_line_runs(const uint8_t* buf, uint8_t* mask, size_t first, size_t stride,
                           unsigned len, unsigned max_depth, const uint8_t ref[4])
{
    unsigned run = 0;
    while (run < len && px_is_bar(buf + (first + run * stride) * 4, ref))
        run++;
    if (run == len) {
        for (unsigned i = 0; i < len; i++)
            mask[first + i * stride] = 1;
        return;
    }
    if (run <= max_depth) {
        for (unsigned i = 0; i < run; i++)
            mask[first + i * stride] = 1;
    }
    run = 0;
    while (run < len && px_is_bar(buf + (first + (len - 1 - run) * stride) * 4, ref))
        run++;
    if (run <= max_depth) {
        for (unsigned i = 0; i < run; i++)
            mask[first + (len - 1 - i) * stride] = 1;
    }
}

/* Builds the bar mask (1 = bar pixel). Returns the number of bar pixels
 * found, and tells whether the bars are transparent. */
static size_t detect_bars(const uint8_t* buf, unsigned w, unsigned h, uint8_t* mask,
                          int* transparent)
{
    uint8_t ref[4];
    memset(mask, 0, (size_t)w * h);
    if (!pick_bar_ref(buf, w, h, ref))
        return 0;
    *transparent = ref[3] == 0;

    /* Transparent bars can't be mistaken for dark picture content, so they
     * may be as deep as needed (heavily zoomed out or rotated picture). */
    unsigned max_w = *transparent ? w : (unsigned)(w * MAX_BAR_FRACTION);
    unsigned max_h = *transparent ? h : (unsigned)(h * MAX_BAR_FRACTION);
    for (unsigned y = 0; y < h; y++)
        mark_line_runs(buf, mask, (size_t)y * w, 1, w, max_w, ref);
    for (unsigned x = 0; x < w; x++)
        mark_line_runs(buf, mask, x, w, h, max_h, ref);

    size_t count = 0;
    for (size_t i = 0; i < (size_t)w * h; i++)
        count += mask[i];
    /* A frame that is mostly an opaque flat colour is a fade or a title
     * over a flat background, not a padded picture. */
    if (!*transparent && count > (size_t)w * h * MAX_BAR_AREA_FRACTION)
        return 0;
    return count;
}

/* ---- picture geometry ----------------------------------------------------
 *
 * The picture is modelled as a rectangle of any orientation: centre, unit
 * vectors along its width and height, and half sizes. Its outline is found
 * from the boundary between picture and bars:
 *   1. the dominant edge orientation (modulo 90 degrees) gives a first
 *      estimate of the rotation;
 *   2. boundary points are split into the four sides, and a line fit on
 *      each side refines the rotation;
 *   3. each side's position is the mean position of its points; a side
 *      with no visible point lies off-frame, and the extent of the visible
 *      picture is used instead.
 * The extended rectangle has the same centre and orientation, and the
 * frame's aspect ratio: it is what the frame itself would look like with
 * the same transform applied, i.e. the picture plus its extended edges.
 */

typedef struct {
    double cx, cy; /* centre, in pixels from the frame's top-left corner */
    double ux, uy; /* unit vector along the picture width */
    double vx, vy; /* unit vector along the picture height */
    double pw, ph; /* picture half width/height */
    double ew, eh; /* extended rectangle half width/height */
} geometry_t;

typedef struct {
    float x, y; /* pixel centre, relative to the frame centre */
    float gx, gy; /* coverage gradient, pointing into the picture */
} edge_point_t;

enum { SIDE_U_MIN, SIDE_U_MAX, SIDE_V_MIN, SIDE_V_MAX, SIDE_NONE };

static double coverage(const uint8_t* buf, const uint8_t* mask, unsigned w, unsigned h, int x,
                       int y, int transparent)
{
    if (x < 0)
        x = 0;
    if (x >= (int)w)
        x = (int)w - 1;
    if (y < 0)
        y = 0;
    if (y >= (int)h)
        y = (int)h - 1;
    size_t i = (size_t)y * w + x;
    if (mask[i])
        return 0.0;
    return transparent ? buf[i * 4 + 3] / 255.0 : 1.0;
}

/* Collects the picture pixels touching a bar pixel, with the Sobel
 * gradient of the coverage there. Returns the number of points, or -1 on
 * allocation failure. */
static long collect_edge_points(const uint8_t* buf, const uint8_t* mask, unsigned w, unsigned h,
                                int transparent, edge_point_t** out)
{
    size_t cap = 1024, count = 0;
    edge_point_t* pts = malloc(cap * sizeof(*pts));
    if (!pts)
        return -1;
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            if (mask[i])
                continue;
            int touches = (x > 0 && mask[i - 1]) || (x + 1 < w && mask[i + 1])
                || (y > 0 && mask[i - w]) || (y + 1 < h && mask[i + w]);
            if (!touches)
                continue;
            int xi = (int)x, yi = (int)y;
            double c[3][3];
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    c[dy + 1][dx + 1] = coverage(buf, mask, w, h, xi + dx, yi + dy, transparent);
            double gx = (c[0][2] + 2 * c[1][2] + c[2][2]) - (c[0][0] + 2 * c[1][0] + c[2][0]);
            double gy = (c[2][0] + 2 * c[2][1] + c[2][2]) - (c[0][0] + 2 * c[0][1] + c[0][2]);
            if (gx == 0.0 && gy == 0.0)
                continue;
            if (count == cap) {
                cap *= 2;
                edge_point_t* grown = realloc(pts, cap * sizeof(*pts));
                if (!grown) {
                    free(pts);
                    return -1;
                }
                pts = grown;
            }
            pts[count].x = (float)(x + 0.5 - w / 2.0);
            pts[count].y = (float)(y + 0.5 - h / 2.0);
            pts[count].gx = (float)gx;
            pts[count].gy = (float)gy;
            count++;
        }
    }
    *out = pts;
    return (long)count;
}

/* Side of the rectangle (axes u, v) an edge point belongs to, from its
 * gradient direction; SIDE_NONE for ambiguous (corner) points. */
static int classify_point(const edge_point_t* p, double ux, double uy, double vx, double vy)
{
    double gu = p->gx * ux + p->gy * uy;
    double gv = p->gx * vx + p->gy * vy;
    if (fabs(gu) > 2.0 * fabs(gv))
        return gu < 0 ? SIDE_U_MAX : SIDE_U_MIN;
    if (fabs(gv) > 2.0 * fabs(gu))
        return gv < 0 ? SIDE_V_MAX : SIDE_V_MIN;
    return SIDE_NONE;
}

static int estimate_geometry(const uint8_t* buf, const uint8_t* mask, unsigned w, unsigned h,
                             int transparent, geometry_t* g)
{
    edge_point_t* pts = NULL;
    long count = collect_edge_points(buf, mask, w, h, transparent, &pts);
    if (count <= 0) {
        free(pts);
        return 0;
    }

    /* Edge normals of a rectangle are 90 degrees apart: quadrupling their
     * angle maps all four onto the same direction. */
    double sc = 0.0, ss = 0.0;
    for (long k = 0; k < count; k++) {
        double m2 = (double)pts[k].gx * pts[k].gx + (double)pts[k].gy * pts[k].gy;
        double phi = atan2(pts[k].gy, pts[k].gx);
        sc += m2 * cos(4.0 * phi);
        ss += m2 * sin(4.0 * phi);
    }
    double theta = atan2(ss, sc) / 4.0;

    double pos[4];
    int known[4];
    for (int iter = 0; iter < 4; iter++) {
        double ux = cos(theta), uy = sin(theta), vx = -uy, vy = ux;
        double n[4] = { 0 }, sa[4] = { 0 }, sb[4] = { 0 }, saa[4] = { 0 }, sab[4] = { 0 };
        for (long k = 0; k < count; k++) {
            int side = classify_point(&pts[k], ux, uy, vx, vy);
            if (side == SIDE_NONE)
                continue;
            double s = pts[k].x * ux + pts[k].y * uy;
            double t = pts[k].x * vx + pts[k].y * vy;
            /* a: coordinate along the side, b: across it */
            double a = side <= SIDE_U_MAX ? t : s;
            double b = side <= SIDE_U_MAX ? s : t;
            n[side] += 1;
            sa[side] += a;
            sb[side] += b;
            saa[side] += a * a;
            sab[side] += a * b;
        }
        double delta = 0.0, weight = 0.0;
        for (int side = 0; side < 4; side++) {
            known[side] = n[side] >= MIN_SIDE_POINTS;
            if (!known[side])
                continue;
            pos[side]
                = sb[side] / n[side] + (side == SIDE_U_MIN || side == SIDE_V_MIN ? -0.5 : 0.5);
            double den = n[side] * saa[side] - sa[side] * sa[side];
            if (den <= n[side] * n[side])
                continue;
            double slope = (n[side] * sab[side] - sa[side] * sb[side]) / den;
            /* A side along v tilts by -slope, a side along u by +slope. */
            delta += n[side] * (side <= SIDE_U_MAX ? -atan(slope) : atan(slope));
            weight += n[side];
        }
        if (weight > 0.0 && iter < 3)
            theta += delta / weight;
    }
    free(pts);

    double ux = cos(theta), uy = sin(theta), vx = -uy, vy = ux;
    if (!(known[SIDE_U_MIN] && known[SIDE_U_MAX] && known[SIDE_V_MIN] && known[SIDE_V_MAX])) {
        /* Off-frame sides: fall back to the extent of the visible picture. */
        double lo_s = HUGE_VAL, hi_s = -HUGE_VAL, lo_t = HUGE_VAL, hi_t = -HUGE_VAL;
        for (unsigned y = 0; y < h; y++) {
            for (unsigned x = 0; x < w; x++) {
                if (mask[(size_t)y * w + x])
                    continue;
                double px = x + 0.5 - w / 2.0, py = y + 0.5 - h / 2.0;
                double s = px * ux + py * uy, t = px * vx + py * vy;
                if (s < lo_s)
                    lo_s = s;
                if (s > hi_s)
                    hi_s = s;
                if (t < lo_t)
                    lo_t = t;
                if (t > hi_t)
                    hi_t = t;
            }
        }
        if (!known[SIDE_U_MIN])
            pos[SIDE_U_MIN] = lo_s - 0.5;
        if (!known[SIDE_U_MAX])
            pos[SIDE_U_MAX] = hi_s + 0.5;
        if (!known[SIDE_V_MIN])
            pos[SIDE_V_MIN] = lo_t - 0.5;
        if (!known[SIDE_V_MAX])
            pos[SIDE_V_MAX] = hi_t + 0.5;
    }
    double hu = (pos[SIDE_U_MAX] - pos[SIDE_U_MIN]) / 2.0;
    double hv = (pos[SIDE_V_MAX] - pos[SIDE_V_MIN]) / 2.0;
    if (hu < 1.0 || hv < 1.0)
        return 0;
    double mu = (pos[SIDE_U_MAX] + pos[SIDE_U_MIN]) / 2.0;
    double mv = (pos[SIDE_V_MAX] + pos[SIDE_V_MIN]) / 2.0;
    g->cx = w / 2.0 + mu * ux + mv * vx;
    g->cy = h / 2.0 + mu * uy + mv * vy;

    /* The rotation is only known modulo 90 degrees. An upright picture
     * keeps its horizontal axis as width (portrait clips stay portrait);
     * a rotated one is assumed to be landscape, its longer side being its
     * width. */
    if (fabs(theta) > AXIS_ALIGNED_EPS && hv > hu) {
        g->ux = vx;
        g->uy = vy;
        g->vx = ux;
        g->vy = uy;
        g->pw = hv;
        g->ph = hu;
    } else {
        g->ux = ux;
        g->uy = uy;
        g->vx = vx;
        g->vy = vy;
        g->pw = hu;
        g->ph = hv;
    }

    double aspect = (double)w / (double)h;
    if (g->pw / g->ph < aspect) {
        g->ew = g->ph * aspect;
        g->eh = g->ph;
    } else {
        g->ew = g->pw;
        g->eh = g->pw / aspect;
    }
    return 1;
}

/* ---- stretch fill ------------------------------------------------------ */

/* Maps a coordinate along one rectangle axis (picture half size 'hp',
 * extended half size 'he') to the picture coordinate it is copied from:
 * across the extension, the 'src_len' picture pixels next to the edge are
 * stretched outwards. Coordinates inside the picture map to themselves;
 * beyond the extended rectangle, the outermost source is repeated. */
static double map_axis(double q, double hp, double he, double src_len)
{
    double inner = hp - EDGE_INSET;
    if (inner < 0.5)
        inner = 0.5;
    double aq = fabs(q);
    if (aq <= inner)
        return q;
    double span = he - inner;
    double frac = (aq - inner) / span;
    if (frac > 1.0)
        frac = 1.0;
    double len = src_len < 2.0 * inner ? src_len : 2.0 * inner;
    if (len < 1.0)
        len = 1.0;
    double mapped = inner - frac * (len - 1.0);
    return q < 0 ? -mapped : mapped;
}

/* Bilinear sample at (x, y) (pixel units, top-left origin), weighted by
 * alpha so that transparent neighbours don't darken the result. The
 * returned pixel is opaque. */
static void sample_bilinear(const uint8_t* buf, unsigned w, unsigned h, double x, double y,
                            uint8_t out[4])
{
    x -= 0.5;
    y -= 0.5;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x > w - 1)
        x = w - 1;
    if (y > h - 1)
        y = h - 1;
    unsigned x0 = (unsigned)x, y0 = (unsigned)y;
    unsigned x1 = x0 + 1 < w ? x0 + 1 : x0;
    unsigned y1 = y0 + 1 < h ? y0 + 1 : y0;
    double fx = x - x0, fy = y - y0;
    const uint8_t* p[4] = {
        buf + ((size_t)y0 * w + x0) * 4,
        buf + ((size_t)y0 * w + x1) * 4,
        buf + ((size_t)y1 * w + x0) * 4,
        buf + ((size_t)y1 * w + x1) * 4,
    };
    const double wt[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    double acc[3] = { 0 }, plain[3] = { 0 }, wa = 0.0;
    for (int k = 0; k < 4; k++) {
        double a = wt[k] * p[k][3];
        wa += a;
        for (int c = 0; c < 3; c++) {
            acc[c] += a * p[k][c];
            plain[c] += wt[k] * p[k][c];
        }
    }
    for (int c = 0; c < 3; c++)
        out[c] = (uint8_t)((wa > 1e-6 ? acc[c] / wa : plain[c]) + 0.5);
    out[3] = 255;
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
    size_t n = (size_t)w * h;
    size_t bytes = n * 4;
    const uint8_t* src = (const uint8_t*)inframe;

    memcpy(outframe, inframe, bytes);
    uint8_t* buf = (uint8_t*)outframe;
    if (w < 2 || h < 2)
        return;

    uint8_t* mask = malloc(n);
    uint8_t* filled = malloc(bytes);
    uint8_t* blurred = malloc(bytes);
    uint8_t* tmp = malloc(bytes);
    geometry_t g;
    int transparent = 0;
    if (!mask || !filled || !blurred || !tmp || detect_bars(src, w, h, mask, &transparent) == 0
        || !estimate_geometry(src, mask, w, h, transparent, &g)) {
        free(mask);
        free(filled);
        free(blurred);
        free(tmp);
        return;
    }

    double edge_width_px = inst->edge_width * MAX_EDGE_PX;
    double blur_px = inst->blur_amount * MAX_BLUR_PX;
    int blur_r = (int)(blur_px + 0.5);

    /* Pass 1: the picture laid over its stretched extension, everywhere
     * (also beyond the extended rectangle, so the blur has data there). */
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            const uint8_t* o = src + i * 4;
            double alpha = mask[i] ? 0.0 : (transparent ? o[3] / 255.0 : 1.0);
            if (alpha >= 1.0) {
                memcpy(filled + i * 4, o, 4);
                continue;
            }
            double dx = x + 0.5 - g.cx, dy = y + 0.5 - g.cy;
            double s = map_axis(dx * g.ux + dy * g.uy, g.pw, g.ew, edge_width_px);
            double t = map_axis(dx * g.vx + dy * g.vy, g.ph, g.eh, edge_width_px);
            uint8_t ext[4];
            sample_bilinear(src, w, h, g.cx + s * g.ux + t * g.vx, g.cy + s * g.uy + t * g.vy, ext);
            for (int c = 0; c < 3; c++)
                filled[i * 4 + c] = (uint8_t)(o[c] * alpha + ext[c] * (1.0 - alpha) + 0.5);
            filled[i * 4 + 3] = 255;
        }
    }
    if (blur_r > 0) {
        box_blur_h(filled, tmp, w, h, blur_r);
        box_blur_v(tmp, blurred, w, h, blur_r);
    }

    /* Pass 2: blend the blurred extension in outside the picture (feathered
     * over the blur radius), clipped to the extended rectangle. Picture
     * pixels are left untouched. */
    double feather = blur_r;
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            const uint8_t* o = src + i * 4;
            double oa = mask[i] ? 0.0 : (transparent ? o[3] / 255.0 : 1.0);
            if (oa >= 1.0)
                continue;

            double dx = x + 0.5 - g.cx, dy = y + 0.5 - g.cy;
            double s = fabs(dx * g.ux + dy * g.uy), t = fabs(dx * g.vx + dy * g.vy);
            double inside_ext = (g.ew - s < g.eh - t ? g.ew - s : g.eh - t) + 0.5;
            if (inside_ext <= 0.0)
                continue;
            double ecov = inside_ext >= 1.0 ? 1.0 : inside_ext;

            double outside = (s - g.pw > t - g.ph ? s - g.pw : t - g.ph) + 0.5;
            double a = 0.0;
            if (blur_r > 0 && outside > 0.0)
                a = outside >= feather ? 1.0 : outside / feather;
            double px[3];
            for (int c = 0; c < 3; c++)
                px[c] = filled[i * 4 + c] + (blurred[i * 4 + c] - filled[i * 4 + c]) * a;

            uint8_t* dst = buf + i * 4;
            if (!transparent) {
                /* opaque bar: the bar colour gives way to the extension */
                for (int c = 0; c < 3; c++)
                    dst[c] = (uint8_t)(o[c] + (px[c] - o[c]) * ecov + 0.5);
                continue;
            }
            /* 'px' already holds the picture over the extension; fade from
             * the original pixel towards it by the rectangle coverage. */
            double keep = oa * (1.0 - ecov);
            double out_a = keep + ecov;
            for (int c = 0; c < 3; c++)
                dst[c] = (uint8_t)((o[c] * keep + px[c] * ecov) / out_a + 0.5);
            dst[3] = (uint8_t)(out_a * 255.0 + 0.5);
        }
    }

    free(mask);
    free(filled);
    free(blurred);
    free(tmp);
}

void f0r_update2(f0r_instance_t instance, double time, const uint32_t* inframe1,
                 const uint32_t* inframe2, const uint32_t* inframe3, uint32_t* outframe)
{
    (void)inframe2;
    (void)inframe3;
    f0r_update(instance, time, inframe1, outframe);
}
