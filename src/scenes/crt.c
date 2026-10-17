// SPDX-License-Identifier: GPL-3.0-or-later
// How a cathode ray tube paints a picture, in the look of 90s TV graphics: flat-shaded
// polygons with a painter's sort, an additive "glass" bell, a camera stepped at 12.5 fps,
// and labels on leader lines. The beam runs gun -> deflection coils -> screen and scans
// a raster that fades like phosphor.
#include "core.h"

#include <stddef.h>

#define RATE 12.5f // the picture only changes this often: the jerky 90s render look
#define NSEG 16    // facets around the tube, and the vertex limit of one facet
#define NRING 4    // rings along the bell
#define NL 16      // raster lines on the screen
#define SPL 3      // picture steps per raster line, so the sweep visibly runs left to right
#define FLYS 6     // flyback, in picture steps
#define PERIOD (NL * SPL + FLYS)
#define MAXP 64

// Tube geometry in world units along z; the screen faces +z.
#define Z_SOCK -82.0f
#define Z_NECK0 -80.0f
#define Z_GUN0 -72.0f
#define Z_GUN1 -52.0f
#define Z_COIL0 -27.0f
#define Z_COIL1 -14.0f
#define Z_SCREEN 26.0f
#define R_NECK 7.0f
#define R_GUN 4.5f
#define R_COIL 11.0f
#define SCR_A 42.0f // screen half width
#define SCR_B 32.0f // screen half height
#define RAS_A 32.0f // raster half extents
#define RAS_B 24.0f

#define CX 230
#define CY 150
#define FOCAL 300.0f
#define DIST 150.0f

typedef struct {
    float x, y, z;
} V3;

typedef struct {
    int n, sx[NSEG], sy[NSEG];
    float depth;
    uint16_t c;
} Poly;

static Poly polys[MAXP];
static int npoly;
static float ca, sa, cb, sb; // camera yaw and pitch
static uint16_t bg[H];
static int xl[H], xr[H];

static void init(void) {
    for (int y = 0; y < H; y++) {
        float f = y / (float)(H - 1);
        bg[y] = rgb((int)(14 - 12 * f), (int)(26 - 24 * f), (int)(84 - 74 * f));
    }
}

// world -> camera: yaw about Y, pitch about X, push back along z
static V3 cam(V3 p) {
    float x = p.x * ca + p.z * sa, z = -p.x * sa + p.z * ca;
    V3 r = {x, p.y * cb + z * sb, -p.y * sb + z * cb + DIST};
    return r;
}

static void proj(V3 c, int *sx, int *sy) {
    float k = FOCAL / (c.z < 1 ? 1 : c.z);
    *sx = CX + (int)(c.x * k);
    *sy = CY - (int)(c.y * k);
}

static void proj_w(V3 w, int *sx, int *sy) { proj(cam(w), sx, sy); }

// Two-sided lambert of the facet a,b,c; grazing (optional) is 1 when seen edge-on.
static float shade(V3 a, V3 b, V3 c, float *grazing) {
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len < 1e-6f) {
        if (grazing) *grazing = 0;
        return 0;
    }
    nx /= len;
    ny /= len;
    nz /= len;
    if (grazing) {
        float vl = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
        *grazing = 1.0f - fabsf((nx * a.x + ny * a.y + nz * a.z) / (vl < 1e-6f ? 1 : vl));
    }
    return fabsf(nx * -0.5f + ny * 0.8f + nz * -0.33f); // light from the upper left, camera side
}

static void poly_fill(int n, const int *vx, const int *vy, uint16_t c, int add) {
    int ymin = H, ymax = -1;
    for (int i = 0; i < n; i++) {
        if (vy[i] < ymin) ymin = vy[i];
        if (vy[i] > ymax) ymax = vy[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > H - 1) ymax = H - 1;
    if (ymin > ymax) return;
    for (int y = ymin; y <= ymax; y++) {
        xl[y] = W;
        xr[y] = -1;
    }
    for (int i = 0; i < n; i++) {
        int x0 = vx[i], y0 = vy[i], x1 = vx[(i + 1) % n], y1 = vy[(i + 1) % n];
        if (y0 > y1) {
            int tx = x0, ty = y0;
            x0 = x1;
            y0 = y1;
            x1 = tx;
            y1 = ty;
        }
        int ya = y0 < 0 ? 0 : y0, yb = y1 > H - 1 ? H - 1 : y1;
        for (int y = ya; y <= yb; y++) {
            int a, b;
            if (y1 == y0) {
                a = x0 < x1 ? x0 : x1;
                b = x0 < x1 ? x1 : x0;
            } else {
                a = b = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
            }
            if (a < xl[y]) xl[y] = a;
            if (b > xr[y]) xr[y] = b;
        }
    }
    for (int y = ymin; y <= ymax; y++) {
        int a = xl[y] < 0 ? 0 : xl[y], b = xr[y] > W - 1 ? W - 1 : xr[y];
        uint16_t *row = buf + y * W;
        if (add)
            for (int x = a; x <= b; x++) row[x] = add16(row[x], c);
        else
            for (int x = a; x <= b; x++) row[x] = c;
    }
}

// Opaque facets are collected and painter-sorted; glass is drawn at once, additively.
static void opaque(int n, const V3 *w, uint16_t base) {
    if (npoly >= MAXP || n > NSEG) return;
    Poly *p = &polys[npoly++];
    V3 c[NSEG];
    float depth = 0;
    for (int i = 0; i < n; i++) {
        c[i] = cam(w[i]);
        depth += c[i].z;
        proj(c[i], &p->sx[i], &p->sy[i]);
    }
    p->n = n;
    p->depth = depth / n;
    p->c = scale16(base, (int)((0.3f + 0.7f * shade(c[0], c[1], c[2], NULL)) * 256));
}

static void glass(int n, const V3 *w) {
    V3 c[NSEG];
    int sx[NSEG], sy[NSEG];
    if (n > NSEG) return;
    for (int i = 0; i < n; i++) {
        c[i] = cam(w[i]);
        proj(c[i], &sx[i], &sy[i]);
    }
    float g, lam = shade(c[0], c[1], c[2], &g);
    poly_fill(n, sx, sy, scale16(rgb(70, 100, 190), (int)(30 + 70 * lam + 80 * g * g)), 1);
}

// Cross-section of the bell: circle at the neck (s = 0), rounded rectangle at the screen (s = 1).
static V3 ring_pt(int i, float s, float z) {
    float a = i * 2 * PI / NSEG, c = cosf(a), sn = sinf(a);
    float rx = SCR_A * (c < 0 ? -1 : 1) * sqrtf(fabsf(c)), ry = SCR_B * (sn < 0 ? -1 : 1) * sqrtf(fabsf(sn));
    float e = powf(s, 1.4f);
    V3 p = {R_NECK * c * (1 - e) + rx * e, R_NECK * sn * (1 - e) + ry * e, z};
    return p;
}

static void cylinder(float r, float z0, float z1, int n, uint16_t c, int solid, int caps) {
    V3 r0[NSEG], r1[NSEG];
    if (n > NSEG) n = NSEG;
    for (int i = 0; i < n; i++) {
        float a = i * 2 * PI / n;
        r0[i] = (V3){r * cosf(a), r * sinf(a), z0};
        r1[i] = (V3){r * cosf(a), r * sinf(a), z1};
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        V3 q[4] = {r0[i], r0[j], r1[j], r1[i]};
        if (solid) opaque(4, q, c);
        else glass(4, q);
    }
    if (caps) {
        opaque(n, r0, c);
        opaque(n, r1, c);
    }
}

static void line_w(V3 a, V3 b, uint16_t c) {
    int x0, y0, x1, y1;
    proj_w(a, &x0, &y0);
    proj_w(b, &x1, &y1);
    line(x0, y0, x1, y1, c);
}

static void glow_line(int x0, int y0, int x1, int y1, uint16_t bright, uint16_t dim) {
    line(x0 + 1, y0, x1 + 1, y1, dim);
    line(x0 - 1, y0, x1 - 1, y1, dim);
    line(x0, y0 + 1, x1, y1 + 1, dim);
    line(x0, y0 - 1, x1, y1 - 1, dim);
    line(x0, y0, x1, y1, bright);
}

static void label(const char *s, int x, int y, int ax, int ay) {
    uint16_t c = rgb(255, 230, 120);
    int w = text_w(&fsml, s, 1);
    int ly = ay < y ? y - 2 : y + fsml.h + 1;
    line(x + w / 2, ly, ax, ay, c);
    circle(ax, ay, 2, c);
    rect(x - 3, y - 2, w + 6, fsml.h + 3, rgb(0, 0, 0));
    text(&fsml, s, x, y, 1, c);
}

static void frame(float t, float dt) {
    (void)dt;
    int step = (int)(t * RATE);
    float tq = step / RATE;
    float yaw = 0.85f + 0.35f * sinf(tq * 0.3f), pitch = 0.35f;
    ca = cosf(yaw);
    sa = sinf(yaw);
    cb = cosf(pitch);
    sb = sinf(pitch);
    for (int y = 0; y < H; y++) {
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++) row[x] = bg[y];
    }

    // opaque parts: socket, gun, coils, screen
    npoly = 0;
    V3 sock[NSEG], scr[NSEG];
    for (int i = 0; i < NSEG; i++) {
        float a = i * 2 * PI / NSEG;
        sock[i] = (V3){R_NECK * cosf(a), R_NECK * sinf(a), Z_SOCK};
        scr[i] = ring_pt(i, 1, Z_SCREEN);
    }
    opaque(NSEG, sock, rgb(70, 70, 80));
    cylinder(R_GUN, Z_GUN0, Z_GUN1, 8, rgb(165, 165, 180), 1, 1);
    cylinder(R_COIL, Z_COIL0, Z_COIL1, NSEG, rgb(210, 115, 45), 1, 1);
    opaque(NSEG, scr, rgb(30, 52, 48));
    for (int i = 1; i < npoly; i++) { // far to near
        Poly p = polys[i];
        int j = i - 1;
        for (; j >= 0 && polys[j].depth < p.depth; j--) polys[j + 1] = polys[j];
        polys[j + 1] = p;
    }
    for (int i = 0; i < npoly; i++) poly_fill(polys[i].n, polys[i].sx, polys[i].sy, polys[i].c, 0);

    // glass: neck up to the coils, then the bell
    cylinder(R_NECK, Z_NECK0, Z_COIL0, NSEG, 0, 0, 0);
    V3 rings[NRING][NSEG];
    for (int k = 0; k < NRING; k++)
        for (int i = 0; i < NSEG; i++)
            rings[k][i] = ring_pt(i, k / (float)(NRING - 1), Z_COIL1 + (Z_SCREEN - Z_COIL1) * k / (NRING - 1));
    for (int k = 0; k < NRING - 1; k++)
        for (int i = 0; i < NSEG; i++) {
            int j = (i + 1) % NSEG;
            V3 q[4] = {rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]};
            glass(4, q);
        }

    // edges, like a wireframe laid over the render
    uint16_t edge = rgb(110, 140, 200), wind = rgb(120, 60, 20);
    for (int k = 0; k < NRING; k++)
        for (int i = 0; i < NSEG; i++) line_w(rings[k][i], rings[k][(i + 1) % NSEG], edge);
    for (int i = 0; i < NSEG; i += 2) {
        for (int k = 0; k < NRING - 1; k++) line_w(rings[k][i], rings[k + 1][i], edge);
        V3 a = {R_NECK * cosf(i * 2 * PI / NSEG), R_NECK * sinf(i * 2 * PI / NSEG), Z_NECK0}, b = a;
        b.z = Z_COIL0;
        line_w(a, b, edge);
    }
    for (int i = 0; i < NSEG; i++) {
        float a0 = i * 2 * PI / NSEG, a1 = (i + 1) * 2 * PI / NSEG;
        V3 p = {R_NECK * cosf(a0), R_NECK * sinf(a0), Z_NECK0}, q = {R_NECK * cosf(a1), R_NECK * sinf(a1), Z_NECK0};
        line_w(p, q, edge);
        for (int k = 0; k < 3; k++) { // coil windings
            float z = Z_COIL0 + (Z_COIL1 - Z_COIL0) * (k + 1) / 4;
            V3 u = {R_COIL * cosf(a0), R_COIL * sinf(a0), z}, v = {R_COIL * cosf(a1), R_COIL * sinf(a1), z};
            line_w(u, v, wind);
        }
    }

    // raster on the screen with phosphor persistence; counted in picture steps so that
    // the sweep is monotonic (a line time that is not a multiple of a step would alias)
    int ps = step % PERIOD, cur = ps < NL * SPL ? ps / SPL : -1;
    float frac = (ps % SPL) / (float)SPL, ycur = 0;
    for (int k = 0; k < NL; k++) {
        float y = RAS_B - k * 2 * RAS_B / (NL - 1), x1 = RAS_A;
        int age; // steps since the line was completed
        if (k == cur) {
            x1 = -RAS_A + 2 * RAS_A * frac;
            age = 0;
            ycur = y;
        } else if (cur < 0 || k < cur) {
            age = ps - (k + 1) * SPL;
        } else {
            age = ps + PERIOD - (k + 1) * SPL;
        }
        float b = 1 - age / (float)PERIOD;
        V3 a = {-RAS_A, y, Z_SCREEN - 0.5f}, e = {x1, y, Z_SCREEN - 0.5f};
        line_w(a, e, scale16(rgb(60, 255, 140), (int)(b * 256)));
    }

    // the beam: gun -> deflection point inside the coils -> screen
    V3 g = {0, 0, Z_GUN1}, d = {0, 0, (Z_COIL0 + Z_COIL1) / 2}, s;
    if (cur >= 0) {
        s = (V3){-RAS_A + 2 * RAS_A * frac, ycur, Z_SCREEN - 0.5f};
    } else {
        float f = (ps - NL * SPL) / (float)FLYS; // flyback: blanked, swinging back to the top left
        s = (V3){RAS_A - 2 * RAS_A * f, -RAS_B + 2 * RAS_B * f, Z_SCREEN - 0.5f};
    }
    int gx, gy, dx, dy, sx, sy;
    proj_w(g, &gx, &gy);
    proj_w(d, &dx, &dy);
    proj_w(s, &sx, &sy);
    if (cur >= 0) {
        glow_line(gx, gy, dx, dy, rgb(200, 255, 255), rgb(0, 90, 110));
        glow_line(dx, dy, sx, sy, rgb(200, 255, 255), rgb(0, 90, 110));
        circle(sx, sy, 2, rgb(255, 255, 255));
    } else {
        line(gx, gy, dx, dy, rgb(0, 50, 60));
        line(dx, dy, sx, sy, rgb(0, 50, 60));
    }

    // title, labels in sequence, footer
    const char *title = "CATHODE RAY TUBE";
    text_shadow(&fbig, title, (W - text_w(&fbig, title, 1)) / 2, 10, 1, rgb(255, 255, 255));
    int ax, ay;
    if (tq >= 1.5f && tq < 5.0f) {
        proj_w((V3){0, R_GUN, (Z_GUN0 + Z_GUN1) / 2}, &ax, &ay);
        label("ELECTRON GUN", 16, 236, ax, ay);
    } else if (tq >= 5.0f && tq < 8.5f) {
        proj_w((V3){0, R_COIL, (Z_COIL0 + Z_COIL1) / 2}, &ax, &ay);
        label("DEFLECTION COILS", 16, 58, ax, ay);
    } else if (tq >= 8.5f && tq < 12.0f) {
        const char *s2 = "ELECTRON BEAM";
        label(s2, W - 16 - text_w(&fsml, s2, 1), 58, (dx + sx) / 2, (dy + sy) / 2);
    } else if (tq >= 12.0f && tq < 15.5f) {
        const char *s2 = "PHOSPHOR SCREEN";
        proj_w((V3){0, -SCR_B, Z_SCREEN}, &ax, &ay);
        label(s2, W - 16 - text_w(&fsml, s2, 1), 236, ax, ay);
    }
    text_shadow(&fsml, "bitcircus101.de", (W - text_w(&fsml, "bitcircus101.de", 1)) / 2, 268, 1, rgb(0, 255, 255));
}

const Scene scene_crt = {"crt", 16, init, frame};
