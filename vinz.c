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
#define NUM_STYLES 10

volatile int running = 1;
float speed_multiplier = 1.0f;
int palette = 0;
int style = 0;
int ui_visible = 1;

struct termios orig_termios;

const char* palette_names[NUM_PALETTES] = {
    "Cyberpunk", "Matrix", "Inferno", "Deep Sea", "Cosmic",
    "Dark", "Grey", "Rainbow", "Blood", "Gold",
    "Toxic", "Synthwave", "Heaven", "Void", "Sunrise",
    "Forest", "Cotton Candy", "Rust", "Neon", "Ghost"
};

const char* style_names[NUM_STYLES] = {
    "Liquid Space", "ASCII Flow", "Plasma Waves", "Voronoi Cells",
    "Digital Rain", "Psychedelic", "Julia Fractal", "Hyperspace",
    "Binary Code", "Black Hole"
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
    }
}

void render_pixel(int current_style, float u, float v, float t, int* r_out, int* g_out, int* b_out, char* char_out) {
    t *= speed_multiplier;
    float x, y;
    get_pattern(current_style, u, v, t, &x, &y, char_out);
    
    float r = 0, g = 0, b = 0, val = 0;

    // Apply color palette
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
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "s:p:m:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's': speed_multiplier = atof(optarg); break;
            case 'p': palette = atoi(optarg) % NUM_PALETTES; break;
            case 'm': style = atoi(optarg) % NUM_STYLES; break;
            case 'h': print_help(argv[0]); return 0;
            default: print_help(argv[0]); return 1;
        }
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    enable_raw_mode();

    // Switch to alternate screen buffer and hide cursor
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
        // Handle input
        char c;
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
                style = (style + 1) % NUM_STYLES;
            } else if (c == 'w' || c == 'W') {
                style = (style - 1 + NUM_STYLES) % NUM_STYLES;
            } else if (c == '\033') { // Arrow keys
                usleep(5000); // Give a small window for the rest of the sequence
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'C') { // Right arrow
                            palette = (palette + 1) % NUM_PALETTES;
                        } else if (seq[1] == 'D') { // Left arrow
                            palette = (palette - 1 + NUM_PALETTES) % NUM_PALETTES;
                        } else if (seq[1] == 'B') { // Down arrow
                            style = (style + 1) % NUM_STYLES;
                        } else if (seq[1] == 'A') { // Up arrow
                            style = (style - 1 + NUM_STYLES) % NUM_STYLES;
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

                int is_ascii = (style == 1 || style == 4 || style == 8);

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
            char ui_text[256];
            int text_len = snprintf(ui_text, sizeof(ui_text), 
                " [VinZ] Style: %02d/%02d - %s | Palette: %02d/%02d - %s | W/S: Style | A/D: Color | [H]ide | [Q]uit ", 
                style + 1, NUM_STYLES, style_names[style],
                palette + 1, NUM_PALETTES, palette_names[palette]);
            
            buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "\033[%d;1H\033[48;5;236m\033[38;5;255m", rows);
            buf_pos += snprintf(frame_buffer + buf_pos, buf_size - buf_pos, "%s", ui_text);
            
            for (int i = text_len; i < cols; i++) {
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
