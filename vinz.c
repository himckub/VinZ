#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <termios.h>

#define NUM_PALETTES 20
#define NUM_STYLES_2D 11
#define NUM_STYLES_3D 9

volatile int running = 1;
float speed_multiplier = 1.0f;
int palette = 0;
int style = 0;
int mode_3d = 0;
int ui_visible = 1;

unsigned int current_seed = 0;
unsigned int rand_state = 0;

unsigned int my_rand() {
    rand_state = rand_state * 1664525 + 1013904223;
    return rand_state;
}

float my_rand_f() {
    return (float)(my_rand() & 0xFFFFFF) / (float)0xFFFFFF;
}

int r2d_type, r2d_iters; float r2d_f[8]; int r2d_op[4];
int r3d_shape1, r3d_shape2, r3d_op, r3d_mod;
float r3d_s1[3], r3d_s2[3], r3d_disp_f, r3d_disp_a;

void generate_random_shader(unsigned int seed) {
    current_seed = seed;
    rand_state = seed;
    
    r2d_type = my_rand() % 4;
    r2d_iters = 1 + my_rand() % 4;
    for(int i=0; i<8; i++) r2d_f[i] = (my_rand_f() * 5.0f) - 2.5f;
    for(int i=0; i<4; i++) r2d_op[i] = my_rand() % 4;

    r3d_shape1 = my_rand() % 4;
    r3d_shape2 = my_rand() % 4;
    r3d_op = my_rand() % 4;
    r3d_mod = my_rand() % 4;
    for(int i=0; i<3; i++) r3d_s1[i] = (my_rand_f() * 2.0f) + 0.2f;
    for(int i=0; i<3; i++) r3d_s2[i] = (my_rand_f() * 2.0f) + 0.2f;
    r3d_disp_f = my_rand_f() * 5.0f;
    r3d_disp_a = my_rand_f() * 0.5f;
}

struct termios orig_termios;

const char* palette_names[NUM_PALETTES] = {
    "Cyberpunk", "Matrix", "Inferno", "Deep Sea", "Cosmic",
    "Dark", "Grey", "Rainbow", "Blood", "Gold",
    "Toxic", "Synthwave", "Heaven", "Void", "Sunrise",
    "Forest", "Cotton Candy", "Rust", "Neon", "Ghost"
};

const char* style_names_2d[NUM_STYLES_2D] = {
    "Liquid Space", "ASCII Flow", "Plasma Waves", "Voronoi Cells",
    "Digital Rain", "Psychedelic", "Julia Fractal", "Hyperspace",
    "Binary Code", "Black Hole", "Procedural Gen"
};

const char* style_names_3d[NUM_STYLES_3D] = {
    "Spinning Hypercube", "Blocks Tunnel", "Cubescape",
    "Bouncing Cubes", "Liquid Cube", "Intersecting Rings",
    "Sphere Grid", "Morphing Star", "Procedural Gen"
};

void print_help(const char* prog_name) {
    printf("Vinz - A high-detail terminal art renderer\n\n");
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -s, --speed <float>    Animation speed multiplier (default: 1.0)\n");
    printf("  -p, --palette <int>    Starting color palette (0-19)\n");
    printf("  -m, --style <int>      Starting style mode (0-9)\n");
    printf("  -h, --help             Show this help message\n\n");
    printf("Controls:\n");
    printf("  A/D or Left/Right      : Change Palette (Color)\n");
    printf("  W/S or Up/Down         : Change Style (Pattern)\n");
    printf("  H                      : Toggle UI\n");
    printf("  Q or Ctrl+C            : Quit\n");
}

void handle_sigint(int sig) {
    (void)sig; // suppress warning
    running = 0;
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

float fract(float x) {
    return x - floorf(x);
}

// 3D Math & Raymarching
typedef struct { float x, y, z; } vec3;
vec3 v3_add(vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
vec3 v3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
vec3 v3_mul(vec3 a, float s) { return (vec3){a.x * s, a.y * s, a.z * s}; }
float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float v3_len(vec3 v) { return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); }
vec3 v3_norm(vec3 v) { float l = v3_len(v); return l == 0.0f ? (vec3){0,0,0} : v3_mul(v, 1.0f/l); }

vec3 rot_x(vec3 p, float a) {
    float s = sinf(a), c = cosf(a);
    return (vec3){p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}
vec3 rot_y(vec3 p, float a) {
    float s = sinf(a), c = cosf(a);
    return (vec3){p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}
vec3 rot_z(vec3 p, float a) {
    float s = sinf(a), c = cosf(a);
    return (vec3){p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

float smin(float a, float b, float k) {
    float h = fmaxf(k - fabsf(a - b), 0.0f) / k;
    return fminf(a, b) - h * h * k * (1.0f / 4.0f);
}

float length2(float x, float y) { return sqrtf(x*x + y*y); }

float eval_shape(vec3 p, int type, float s[3]) {
    if (type == 0) {
        return v3_len(p) - s[0] * 1.5f;
    } else if (type == 1) {
        vec3 d = {fabsf(p.x) - s[0], fabsf(p.y) - s[1], fabsf(p.z) - s[2]};
        return fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0f) + v3_len((vec3){fmaxf(d.x,0.0f), fmaxf(d.y,0.0f), fmaxf(d.z,0.0f)});
    } else if (type == 2) {
        float qx = length2(p.x, p.z) - (s[0] * 1.5f);
        return length2(qx, p.y) - (s[1] * 0.5f);
    } else {
        return length2(p.x, p.z) - s[0];
    }
}

float map_3d(vec3 p, int style, float t) {
    if (style == 0) {
        p = rot_x(p, t * 0.4f);
        p = rot_y(p, t * 0.5f);
        vec3 d = {fabsf(p.x) - 1.5f, fabsf(p.y) - 1.5f, fabsf(p.z) - 1.5f};
        float box = fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0f) + v3_len((vec3){fmaxf(d.x,0.0f), fmaxf(d.y,0.0f), fmaxf(d.z,0.0f)});
        float cut = length2(p.x, p.y) - 1.2f;
        cut = fminf(cut, length2(p.y, p.z) - 1.2f);
        cut = fminf(cut, length2(p.z, p.x) - 1.2f);
        return fmaxf(box, -cut);
    } else if (style == 1) {
        p.z += t * 3.0f;
        p = rot_z(p, sinf(p.z * 0.2f + t) * 1.5f);
        float d_xy = sqrtf(p.x*p.x + p.y*p.y) - 2.5f;
        float blocks = sinf(atan2f(p.y, p.x)*6.0f) * sinf(p.z*2.0f) * 0.4f;
        return -d_xy + fmaxf(0.0f, blocks); 
    } else if (style == 2) {
        p.z += t * 0.5f;
        p = rot_z(p, t*0.3f);
        vec3 q = {p.x - floorf(p.x/3.0f)*3.0f - 1.5f, 
                  p.y - floorf(p.y/3.0f)*3.0f - 1.5f, 
                  p.z - floorf(p.z/3.0f)*3.0f - 1.5f};
        vec3 d = {fabsf(q.x) - 0.5f, fabsf(q.y) - 0.5f, fabsf(q.z) - 0.5f};
        float cubes = fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0f) + v3_len((vec3){fmaxf(d.x,0.0f), fmaxf(d.y,0.0f), fmaxf(d.z,0.0f)});
        float tunnel = length2(p.x, p.y) - 2.5f;
        return fmaxf(cubes, -tunnel);
    } else if (style == 3) {
        p.z -= t * 2.0f;
        float wave_bottom = sinf(p.x*1.5f + t)*0.5f + cosf(p.z*1.2f + t)*0.5f;
        float wave_top = sinf(p.x*1.3f - t)*0.5f + cosf(p.z*1.4f - t)*0.5f;
        
        vec3 q_bottom = {p.x - floorf(p.x) - 0.5f, p.y + 2.5f - wave_bottom, p.z - floorf(p.z) - 0.5f};
        vec3 d_bottom = {fabsf(q_bottom.x) - 0.4f, fabsf(q_bottom.y) - 0.4f, fabsf(q_bottom.z) - 0.4f};
        float cubes_bottom = fminf(fmaxf(d_bottom.x, fmaxf(d_bottom.y, d_bottom.z)), 0.0f) + v3_len((vec3){fmaxf(d_bottom.x,0.0f), fmaxf(d_bottom.y,0.0f), fmaxf(d_bottom.z,0.0f)});
        
        vec3 q_top = {p.x - floorf(p.x) - 0.5f, p.y - 2.5f - wave_top, p.z - floorf(p.z) - 0.5f};
        vec3 d_top = {fabsf(q_top.x) - 0.4f, fabsf(q_top.y) - 0.4f, fabsf(q_top.z) - 0.4f};
        float cubes_top = fminf(fmaxf(d_top.x, fmaxf(d_top.y, d_top.z)), 0.0f) + v3_len((vec3){fmaxf(d_top.x,0.0f), fmaxf(d_top.y,0.0f), fmaxf(d_top.z,0.0f)});
        
        return fminf(cubes_bottom, cubes_top);
    } else if (style == 4) {
        p = rot_x(p, t * 0.4f);
        p = rot_y(p, t * 0.5f);
        vec3 d = {fabsf(p.x) - 2.0f, fabsf(p.y) - 2.0f, fabsf(p.z) - 2.0f};
        float box = fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0f) + v3_len((vec3){fmaxf(d.x,0.0f), fmaxf(d.y,0.0f), fmaxf(d.z,0.0f)});
        float wave = sinf(p.x*5.0f + t)*sinf(p.y*5.0f + t)*sinf(p.z*5.0f + t) * 0.2f;
        return box + wave;
    } else if (style == 5) {
        p = rot_x(p, t * 0.3f);
        p = rot_y(p, t * 0.5f);
        vec3 p1 = rot_x(p, t);
        float d1 = length2(length2(p1.x, p1.y) - 2.5f, p1.z) - 0.2f;
        vec3 p2 = rot_y(p, t * 1.2f);
        float d2 = length2(length2(p2.x, p2.z) - 1.8f, p2.y) - 0.2f;
        vec3 p3 = rot_z(p, t * 0.8f);
        float d3 = length2(length2(p3.y, p3.z) - 1.1f, p3.x) - 0.2f;
        return fminf(fminf(d1, d2), d3);
    } else if (style == 6) {
        p.x += t; p.y += t * 0.5f; p.z += t * 1.5f;
        vec3 q = {p.x - floorf(p.x / 2.0f) * 2.0f - 1.0f, 
                  p.y - floorf(p.y / 2.0f) * 2.0f - 1.0f, 
                  p.z - floorf(p.z / 2.0f) * 2.0f - 1.0f};
        return v3_len(q) - 0.4f;
    } else if (style == 7) {
        p = rot_x(p, t * 0.3f);
        p = rot_y(p, t * 0.2f);
        float d = v3_len(p) - 2.0f;
        d += sinf(p.x*3.0f + t)*sinf(p.y*3.0f + t)*sinf(p.z*3.0f + t) * 0.4f;
        return d * 0.8f;
    } else {
        p = rot_x(p, t * 0.3f);
        p = rot_y(p, t * 0.2f);
        
        vec3 p1 = p;
        vec3 p2 = p;
        
        if (r3d_mod == 1) { 
            float s = sinf(r3d_s1[0] * p.y);
            float c = cosf(r3d_s1[0] * p.y);
            p1.x = p.x * c - p.z * s;
            p1.z = p.x * s + p.z * c;
        } else if (r3d_mod == 2) { 
            p1.x = fabsf(p1.x) - r3d_s1[1]*1.5f;
            p1.z = fabsf(p1.z) - r3d_s1[1]*1.5f;
        } else if (r3d_mod == 3) {
            p1 = rot_z(p1, t * 0.5f); 
            p2 = rot_x(p2, -t * 0.4f); 
        }
        
        float s1_mod[3] = {r3d_s1[0] + 0.8f, r3d_s1[1] + 0.8f, r3d_s1[2] + 0.8f};
        float s2_mod[3] = {r3d_s2[0] + 0.8f, r3d_s2[1] + 0.8f, r3d_s2[2] + 0.8f};
        
        float d1 = eval_shape(p1, r3d_shape1, s1_mod);
        float d2 = eval_shape(p2, r3d_shape2, s2_mod);
        
        float disp = sinf(p.x * r3d_disp_f + t) * sinf(p.y * r3d_disp_f - t) * sinf(p.z * r3d_disp_f) * (r3d_disp_a * 0.5f);
        d1 += disp;
        
        float res;
        if (r3d_op == 0) res = fminf(d1, d2);
        else if (r3d_op == 1) res = smin(d1, d2, 0.8f);
        else if (r3d_op == 2) res = fmaxf(d1, -d2);
        else res = fmaxf(d1, d2);
        
        float bounds = v3_len(p) - 3.5f; 
        return fmaxf(res * 0.8f, bounds);
    }
}

vec3 calc_normal(vec3 p, int style, float t) {
    float d = map_3d(p, style, t);
    float e = 0.001f;
    vec3 n = {
        map_3d((vec3){p.x+e, p.y, p.z}, style, t) - d,
        map_3d((vec3){p.x, p.y+e, p.z}, style, t) - d,
        map_3d((vec3){p.x, p.y, p.z+e}, style, t) - d
    };
    return v3_norm(n);
}

void get_pattern(int current_style, float u, float v, float t, float* out_x, float* out_y, char* out_char) {
    *out_char = '\0';
    float x = u, y = v;

    switch (current_style) {
        case 0: // Liquid Space
        case 1: { // ASCII Flow
            x *= 2.5f; y *= 2.5f;
            for(int i = 1; i < 5; i++) {
                float i_f = (float)i;
                float new_x = x + 0.4f / i_f * sinf(i_f * y + t);
                float new_y = y + 0.4f / i_f * cosf(i_f * x + t);
                x = new_x; y = new_y;
            }
            *out_x = x; *out_y = y;
            if (current_style == 1) {
                float intensity = fabsf(sinf(x) * cosf(y));
                const char* chars = " .:-=+*#%@";
                int char_idx = (int)(intensity * 10.0f);
                if (char_idx > 9) char_idx = 9;
                *out_char = chars[char_idx];
            }
            break;
        }
        case 2: { // Plasma
            *out_x = u * 10.0f + sinf(t) * 2.0f;
            *out_y = v * 10.0f + cosf(t) * 2.0f;
            float v_plasma = sinf(*out_x) + sinf(*out_y) + sinf(*out_x + *out_y + t);
            *out_x = v_plasma; *out_y = v_plasma * 0.5f;
            break;
        }
        case 3: { // Voronoi
            x *= 4.0f; y *= 4.0f;
            float min_dist = 100.0f;
            for (int i=-1; i<=1; i++) {
                for (int j=-1; j<=1; j++) {
                    float cell_x = floorf(x) + i;
                    float cell_y = floorf(y) + j;
                    float rand_x = fract(sinf(cell_x * 12.9898f + cell_y * 78.233f) * 43758.5453f);
                    float rand_y = fract(cosf(cell_x * 39.346f + cell_y * 11.135f) * 43758.5453f);
                    rand_x = 0.5f + 0.5f * sinf(t + rand_x * 6.28f);
                    rand_y = 0.5f + 0.5f * cosf(t + rand_y * 6.28f);
                    float dx = cell_x + rand_x - x;
                    float dy = cell_y + rand_y - y;
                    float dist = sqrtf(dx*dx + dy*dy);
                    if (dist < min_dist) min_dist = dist;
                }
            }
            *out_x = min_dist * 3.0f;
            *out_y = min_dist * 3.0f + t;
            break;
        }
        case 4: { // Digital Rain
            x *= 10.0f; y *= 10.0f;
            float drop = fract(y - t * 3.0f - fract(sinf(floorf(x)) * 43758.5453f) * 10.0f);
            *out_x = drop * 2.0f;
            *out_y = drop * 2.0f;
            if (drop > 0.8f) *out_char = (char)(33 + (int)(fract(sinf(u*v*t)*100.0f)*90.0f));
            else if (drop > 0.3f) *out_char = (char)(33 + (int)(fract(cosf(u*v*t)*100.0f)*90.0f));
            else *out_char = ' ';
            break;
        }
        case 5: { // Psychedelic
            *out_x = sinf(u * u * 20.0f - t) + cosf(v * v * 20.0f + t);
            *out_y = sinf(u * v * 30.0f + t);
            break;
        }
        case 6: { // Julia Fractal
            float zx = u * 2.0f;
            float zy = v * 2.0f;
            float cx = 0.285f * cosf(t * 0.2f);
            float cy = 0.01f * sinf(t * 0.2f);
            int iter = 0;
            float zx2 = zx*zx, zy2 = zy*zy;
            while (zx2 + zy2 < 4.0f && iter < 24) {
                zy = 2.0f * zx * zy + cy;
                zx = zx2 - zy2 + cx;
                zx2 = zx*zx;
                zy2 = zy*zy;
                iter++;
            }
            float smooth_i = (float)iter;
            if (iter < 24) smooth_i = (float)iter + 1.0f - log2f(log2f(zx2 + zy2));
            *out_x = smooth_i * 0.15f;
            *out_y = smooth_i * 0.15f + t;
            break;
        }
        case 7: { // Hyperspace
            float py = fabsf(v) + 0.05f; 
            float px = u / py;
            float pz = 1.0f / py;
            *out_x = px + t;
            *out_y = pz - t * 4.0f;
            float gx = fabsf(fract(*out_x) - 0.5f);
            float gy = fabsf(fract(*out_y) - 0.5f);
            float line = (gx < 0.1f || gy < 0.1f) ? 1.0f : 0.0f;
            float fog = fmaxf(0.0f, 1.0f - pz * 0.1f);
            *out_x = line * fog * 2.0f; 
            *out_y = line * fog * 2.0f;
            break;
        }
        case 8: { // Binary Matrix
            x *= 20.0f; y *= 20.0f;
            float val = fract(sinf(floorf(x)*12.9898f + floorf(y)*78.233f + floorf(t*2.0f)) * 43758.5453f);
            *out_x = val; *out_y = val;
            *out_char = val > 0.5f ? '1' : '0';
            break;
        }
        case 9: { // Black Hole
            float angle = atan2f(v, u);
            float radius = sqrtf(u*u + v*v);
            float swirl = angle + 1.0f / (radius + 0.1f) + t * 2.0f;
            *out_x = cosf(swirl) * radius * 10.0f;
            *out_y = sinf(swirl) * radius * 10.0f;
            break;
        }
        case 10: {
            float cx = u * 5.0f * r2d_f[4], cy = v * 5.0f * r2d_f[5];
            
            if (r2d_type == 0) {
                for (int i = 0; i < r2d_iters; i++) {
                    float nx = cx, ny = cy;
                    if (r2d_op[0] == 0) nx += sinf(cy * r2d_f[0] + t * r2d_f[1]);
                    else if (r2d_op[0] == 1) nx += cosf(cy * r2d_f[0] - t * r2d_f[1]);
                    else nx += sinf(cx * r2d_f[0] + t * r2d_f[1]) * cosf(cy * r2d_f[0]);

                    if (r2d_op[1] == 0) ny += cosf(cx * r2d_f[2] + t * r2d_f[3]);
                    else if (r2d_op[1] == 1) ny += sinf(cx * r2d_f[2] - t * r2d_f[3]);
                    else ny += cosf(cy * r2d_f[2] + t * r2d_f[3]) * sinf(cx * r2d_f[2]);

                    cx = nx; cy = ny;
                }
            } else if (r2d_type == 1) {
                for (int i = 0; i < r2d_iters * 2; i++) {
                    cx = fabsf(cx) - r2d_f[0];
                    cy = fabsf(cy) - r2d_f[1];
                    float angle = t * r2d_f[2];
                    float s = sinf(angle), c = cosf(angle);
                    float nx = cx * c - cy * s;
                    float ny = cx * s + cy * c;
                    cx = nx * r2d_f[3]; cy = ny * r2d_f[3];
                }
            } else if (r2d_type == 2) {
                float sum = 0.0f;
                for (int i = 1; i <= r2d_iters + 1; i++) {
                    float fi = (float)i;
                    sum += sinf(cx * r2d_f[0] * fi + t * r2d_f[1]) * cosf(cy * r2d_f[2] * fi - t * r2d_f[3]);
                    cx += r2d_f[6]; cy += r2d_f[7];
                }
                cx = sum; cy = sum * r2d_f[4];
            } else {
                float dist = sqrtf(cx*cx + cy*cy);
                float angle = atan2f(cy, cx);
                for (int i = 0; i < r2d_iters; i++) {
                    angle += sinf(dist * r2d_f[0] - t * r2d_f[1]) * r2d_f[2];
                    dist += cosf(angle * r2d_f[3] + t * r2d_f[4]) * r2d_f[5];
                }
                cx = cosf(angle) * dist;
                cy = sinf(angle) * dist;
            }
            
            *out_x = cx; *out_y = cy;
            break;
        }
    }
}
void render_pixel(int current_style, float u, float v, float t, int* r_out, int* g_out, int* b_out, char* char_out) {
    t *= speed_multiplier;
    float x = 0, y = 0;
    float r = 0, g = 0, b = 0, val = 0;
    float intensity_mod = 1.0f;

    if (mode_3d) {
        *char_out = '\0';
        vec3 ro = {0.0f, 0.0f, -4.0f};
        if (current_style == 1) ro.z = -1.0f; // Tunnel
        if (current_style == 2) ro.z = -2.0f; // Cubescape
        if (current_style == 3) ro.z = -1.0f; // Bouncing Cubes
        if (current_style == 6) ro.z = -1.0f; // Grid
        
        vec3 rd = v3_norm((vec3){u * 1.5f, v * 1.5f, 1.0f});
        
        float total_d = 0.0f;
        vec3 p = ro;
        for(int i = 0; i < 64; i++) {
            p = v3_add(ro, v3_mul(rd, total_d));
            float d = map_3d(p, current_style, t);
            if (d < 0.001f || total_d > 20.0f) break;
            total_d += d;
        }

        if (total_d < 20.0f) {
            vec3 n = calc_normal(p, current_style, t);
            vec3 light_dir = v3_norm((vec3){sinf(t)*2.0f, 1.0f, -1.0f});
            float diff = fmaxf(v3_dot(n, light_dir), 0.0f);
            float ambient = 0.2f;
            float depth_darken = fmaxf(1.0f - (total_d / 20.0f), 0.0f);
            
            float intensity = (diff * 0.8f + ambient) * depth_darken;
            x = intensity * 2.0f;
            y = intensity * 2.0f;
            intensity_mod = intensity * 2.0f;
        } else {
            intensity_mod = 0.0f;
            x = u;
            y = v;
        }
    } else {
        get_pattern(current_style, u, v, t, &x, &y, char_out);
    }
    
    switch (palette) {
        case 0: r = 0.5f + 0.5f * sinf(x + t); g = 0.5f + 0.5f * sinf(y + t + 2.0f); b = 0.5f + 0.5f * sinf(x + y + t + 4.0f); break; // Cyberpunk
        case 1: r = 0.1f + 0.1f * sinf(x + t); g = 0.5f + 0.5f * sinf(y + t + 2.0f); b = 0.1f + 0.1f * sinf(x + y + t + 4.0f); break; // Matrix
        case 2: r = 0.6f + 0.4f * sinf(y + t); g = 0.3f + 0.3f * sinf(x + t + 2.0f); b = 0.0f + 0.1f * sinf(x + y + t + 4.0f); break; // Inferno
        case 3: r = 0.0f + 0.2f * sinf(x + t); g = 0.4f + 0.4f * sinf(y + t + 2.0f); b = 0.6f + 0.4f * sinf(x + y + t + 4.0f); break; // Deep Sea
        case 4: r = 0.5f + 0.5f * sinf(x + t); g = 0.1f + 0.1f * sinf(y + t + 2.0f); b = 0.6f + 0.4f * sinf(x + y + t + 4.0f); break; // Cosmic
        case 5: val = 0.3f + 0.3f * sinf(x + t); r = val; g = val; b = val; break; // Dark
        case 6: val = 0.5f + 0.4f * sinf(x + t); r = val; g = val; b = val; break; // Grey
        case 7: r = 0.5f + 0.5f * sinf(x + t * 2.0f); g = 0.5f + 0.5f * sinf(y + t * 2.0f + 2.0f); b = 0.5f + 0.5f * sinf(x + y + t * 2.0f + 4.0f); break; // Rainbow
        case 8: r = 0.6f + 0.4f * sinf(x + t); g = 0.0f; b = 0.0f; break; // Blood
        case 9: r = 0.8f + 0.2f * sinf(x + t); g = 0.6f + 0.3f * sinf(y + t); b = 0.1f + 0.1f * sinf(x + y + t); break; // Gold
        case 10: r = 0.3f + 0.3f * sinf(y + t); g = 0.7f + 0.3f * sinf(x + t); b = 0.0f; break; // Toxic
        case 11: r = 0.8f + 0.2f * sinf(x + t); g = 0.1f + 0.1f * sinf(y + t); b = 0.8f + 0.2f * sinf(x + y + t); break; // Synthwave
        case 12: r = 0.7f + 0.3f * sinf(x + t); g = 0.8f + 0.2f * sinf(y + t); b = 0.9f + 0.1f * sinf(x + y + t); break; // Heaven
        case 13: r = 0.2f + 0.2f * sinf(x + t); g = 0.0f; b = 0.4f + 0.3f * sinf(y + t); break; // Void
        case 14: r = 0.9f + 0.1f * sinf(x + t); g = 0.4f + 0.3f * sinf(y + t); b = 0.2f + 0.2f * sinf(x + y + t); break; // Sunrise
        case 15: r = 0.2f + 0.1f * sinf(x + t); g = 0.4f + 0.2f * sinf(y + t); b = 0.1f + 0.1f * sinf(x + y + t); break; // Forest
        case 16: r = 0.8f + 0.2f * sinf(x + t); g = 0.5f + 0.2f * sinf(y + t); b = 0.7f + 0.3f * sinf(x + y + t); break; // Cotton Candy
        case 17: r = 0.6f + 0.2f * sinf(x + t); g = 0.2f + 0.1f * sinf(y + t); b = 0.05f; break; // Rust
        case 18: r = 0.5f + 0.5f * sinf(x * 2.0f + t * 3.0f); g = 0.5f + 0.5f * sinf(y * 2.0f + t * 3.0f + 2.0f); b = 0.5f + 0.5f * sinf((x + y) * 2.0f + t * 3.0f + 4.0f); break; // Neon
        case 19: val = 0.4f + 0.3f * sinf(x + t); r = val - 0.05f; g = val; b = val + 0.1f; break; // Ghost
        default: r = 0.5f; g = 0.5f; b = 0.5f; break;
    }

    if (mode_3d) {
        r *= intensity_mod;
        g *= intensity_mod;
        b *= intensity_mod;
    }
    
    // Clamp before contrast
    r = fmaxf(0.0f, fminf(1.0f, r));
    g = fmaxf(0.0f, fminf(1.0f, g));
    b = fmaxf(0.0f, fminf(1.0f, b));

    // Smoothstep contrast
    r = r * r * (3.0f - 2.0f * r);
    g = g * g * (3.0f - 2.0f * g);
    b = b * b * (3.0f - 2.0f * b);

    *r_out = (int)(r * 255.0f);
    *g_out = (int)(g * 255.0f);
    *b_out = (int)(b * 255.0f);
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"speed",   required_argument, 0, 's'},
        {"palette", required_argument, 0, 'p'},
        {"style",   required_argument, 0, 'm'},
        {"seed",    required_argument, 0, 'r'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    int start_seed = -1;
    
    while ((opt = getopt_long(argc, argv, "s:p:m:r:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's': speed_multiplier = atof(optarg); break;
            case 'p': palette = atoi(optarg) % NUM_PALETTES; break;
            case 'm': 
                style = atoi(optarg); 
                if (style >= NUM_STYLES_2D) {
                    mode_3d = 1;
                    style = style - NUM_STYLES_2D;
                    if (style >= NUM_STYLES_3D) style = NUM_STYLES_3D - 1;
                }
                break;
            case 'r': start_seed = atoi(optarg); break;
            case 'h': print_help(argv[0]); return 0;
            default: print_help(argv[0]); return 1;
        }
    }

    if (start_seed != -1) {
        generate_random_shader(start_seed);
        style = NUM_STYLES_2D - 1; // Default to 2D random
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    enable_raw_mode();

    printf("\033[?1049h\033[?25l\033[?7l");
    fflush(stdout);

    struct winsize w;
    double start_time = get_time();

    size_t buf_size = 4 * 1024 * 1024;
    char* frame_buffer = malloc(buf_size);
    if (!frame_buffer) {
        fprintf(stderr, "Failed to allocate frame buffer\n");
        printf("\033[?1049l\033[?25h\033[?7h");
        return 1;
    }

    while (running) {
        char c;
        int current_num_styles = mode_3d ? NUM_STYLES_3D : NUM_STYLES_2D;
        
        while (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == 'q' || c == 'Q' || c == 3) {
                running = 0;
            } else if (c == 'h' || c == 'H') {
                ui_visible = !ui_visible;
            } else if (c == 'd' || c == 'D') {
                palette = (palette + 1) % NUM_PALETTES;
            } else if (c == 'a' || c == 'A') {
                palette = (palette - 1 + NUM_PALETTES) % NUM_PALETTES;
            } else if (c == 's' || c == 'S') {
                style = (style + 1) % current_num_styles;
            } else if (c == 'w' || c == 'W') {
                style = (style - 1 + current_num_styles) % current_num_styles;
            } else if (c == 't' || c == 'T' || c == '3') {
                mode_3d = !mode_3d;
                current_num_styles = mode_3d ? NUM_STYLES_3D : NUM_STYLES_2D;
                if (style >= current_num_styles) style = current_num_styles - 1;
            } else if (c == 'r' || c == 'R') {
                struct timeval rtv;
                gettimeofday(&rtv, NULL);
                unsigned int seed = rtv.tv_sec ^ rtv.tv_usec;
                generate_random_shader(seed);
                style = mode_3d ? (NUM_STYLES_3D - 1) : (NUM_STYLES_2D - 1);
            } else if (c == '\033') {
                usleep(5000);
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'C') {
                            palette = (palette + 1) % NUM_PALETTES;
                        } else if (seq[1] == 'D') {
                            palette = (palette - 1 + NUM_PALETTES) % NUM_PALETTES;
                        } else if (seq[1] == 'B') {
                            style = (style + 1) % current_num_styles;
                        } else if (seq[1] == 'A') {
                            style = (style - 1 + current_num_styles) % current_num_styles;
                        }
                    }
                }
            }
        }

        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int cols = w.ws_col;
        int rows = w.ws_row;

        if (cols == 0 || rows == 0) {
            usleep(16000);
            continue;
        }

        double t = get_time() - start_time;
        int buf_pos = 0;

        buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "\033[H");

        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                float aspect = (float)cols / (float)(rows * 2);
                float u1 = ((float)x / cols) * 2.0f - 1.0f;
                float v1 = ((float)(y * 2) / (rows * 2)) * 2.0f - 1.0f;
                u1 *= aspect;

                int is_ascii = (!mode_3d && (style == 1 || style == 4 || style == 8));

                if (is_ascii) {
                    // For pure ASCII styles, v maps directly to the character cell center
                    float va = ((float)y / rows) * 2.0f - 1.0f;
                    int r, g, b;
                    char c_out;
                    render_pixel(style, u1, va, (float)t, &r, &g, &b, &c_out);
                    
                    buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos,
                        "\033[38;2;%d;%d;%dm%c", r, g, b, c_out ? c_out : ' ');
                } else {
                    float u2 = ((float)x / cols) * 2.0f - 1.0f;
                    float v2 = ((float)(y * 2 + 1) / (rows * 2)) * 2.0f - 1.0f;
                    u2 *= aspect;

                    int r1, g1, b1, r2, g2, b2;
                    char c1, c2;
                    render_pixel(style, u1, v1, (float)t, &r1, &g1, &b1, &c1);
                    render_pixel(style, u2, v2, (float)t, &r2, &g2, &b2, &c2);

                    buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos,
                        "\033[48;2;%d;%d;%dm\033[38;2;%d;%d;%dm▀",
                        r2, g2, b2, r1, g1, b1);
                }
            }
            if (y < rows - 1) {
                buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "\033[0m\n");
            }
        }
        
        buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "\033[0m");

        // Render UI over the last line if visible
        if (ui_visible && rows > 0) {
            int current_num_styles = mode_3d ? NUM_STYLES_3D : NUM_STYLES_2D;
            const char* current_style_name = mode_3d ? style_names_3d[style] : style_names_2d[style];
            
            buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "\033[%d;1H\033[48;5;236m\033[38;5;255m", rows);
            
            char style_display[128];
            if ((!mode_3d && style == NUM_STYLES_2D - 1) || (mode_3d && style == NUM_STYLES_3D - 1)) {
                snprintf(style_display, sizeof(style_display), "%s [Seed: %u]", current_style_name, current_seed);
            } else {
                snprintf(style_display, sizeof(style_display), "%s", current_style_name);
            }
            
            if (mode_3d) {
                buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, 
                    " [VinZ] \033[38;5;213;1m[T] 3D (NEW): ON \033[0m\033[48;5;236m\033[38;5;255m | Style: %02d/%02d - \033[38;5;208;1m%s\033[0m\033[48;5;236m\033[38;5;255m | Palette: %02d/%02d - \033[38;5;45;1m%s\033[0m\033[48;5;236m\033[38;5;255m | [R]andom | W/S: Style | A/D: Color | [H]ide | [Q]uit ", 
                    style + 1, current_num_styles, style_display,
                    palette + 1, NUM_PALETTES, palette_names[palette]);
            } else {
                buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, 
                    " [VinZ] \033[38;5;213;1m[T] 3D (NEW): OFF\033[0m\033[48;5;236m\033[38;5;255m | Style: %02d/%02d - \033[38;5;208;1m%s\033[0m\033[48;5;236m\033[38;5;255m | Palette: %02d/%02d - \033[38;5;45;1m%s\033[0m\033[48;5;236m\033[38;5;255m | [R]andom | W/S: Style | A/D: Color | [H]ide | [Q]uit ", 
                    style + 1, current_num_styles, style_display,
                    palette + 1, NUM_PALETTES, palette_names[palette]);
            }
            
            // Calculate strictly visible characters for padding
            int visible_len = snprintf(NULL, 0, " [VinZ] [T] 3D (NEW): %s | Style: %02d/%02d - %s | Palette: %02d/%02d - %s | [R]andom | W/S: Style | A/D: Color | [H]ide | [Q]uit ", 
                mode_3d ? "ON " : "OFF", style + 1, current_num_styles, style_display, palette + 1, NUM_PALETTES, palette_names[palette]);

            for (int i = visible_len; i < cols; i++) {
                buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, " ");
            }
            buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "\033[0m");
        }

        if (write(STDOUT_FILENO, frame_buffer, buf_pos) == -1) {
            break;
        }
        
        usleep(33000);
    }

    printf("\033[?1049l\033[?25h\033[?7h");
    fflush(stdout);
    free(frame_buffer);

    return 0;
}
