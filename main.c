#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-gamma-control-client-protocol.h"

struct options {
    bool list_only;
    bool reset;
    const char *output_selector;
    double brightness;
    double contrast;
    double gamma;
    double red;
    double green;
    double blue;
};

struct output {
    struct wl_output *wl_output;
    uint32_t global_name;
    uint32_t registry_name;
    int32_t width;
    int32_t height;
    int32_t refresh;
    int32_t scale;
    char *make;
    char *model;
    char *name;
    char *description;
    bool done;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zwlr_gamma_control_manager_v1 *gamma_manager;
    struct output *outputs;
    size_t output_count;
};

struct gamma_request {
    struct output *output;
    struct zwlr_gamma_control_v1 *control;
    uint32_t gamma_size;
    bool failed;
    bool ready;
};

static volatile sig_atomic_t g_should_exit = 0;

static void usage(FILE *stream, const char *argv0) {
    fprintf(stream,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -l, --list                List detected outputs\n"
        "  -o, --output NAME|INDEX   Select a single output, default is all\n"
        "      --brightness VALUE    Brightness offset, range about -1.0..1.0\n"
        "      --contrast VALUE      Contrast multiplier, default 1.0\n"
        "      --gamma VALUE         Gamma exponent, default 1.0\n"
        "      --red VALUE           Red channel multiplier, default 1.0\n"
        "      --green VALUE         Green channel multiplier, default 1.0\n"
        "      --blue VALUE          Blue channel multiplier, default 1.0\n"
        "      --reset               Reset to neutral ramps\n"
        "  -h, --help                Show this help\n"
        "\n"
        "The process keeps running to hold gamma control. Stop it with Ctrl-C to restore defaults.\n",
        argv0);
}

static char *xstrdup(const char *s) {
    if (!s) {
        return NULL;
    }
    char *copy = strdup(s);
    if (!copy) {
        perror("strdup");
        exit(1);
    }
    return copy;
}

static void parse_options(int argc, char **argv, struct options *opts) {
    static const struct option long_options[] = {
        {"list", no_argument, 0, 'l'},
        {"output", required_argument, 0, 'o'},
        {"brightness", required_argument, 0, 1},
        {"contrast", required_argument, 0, 2},
        {"gamma", required_argument, 0, 3},
        {"red", required_argument, 0, 4},
        {"green", required_argument, 0, 5},
        {"blue", required_argument, 0, 6},
        {"reset", no_argument, 0, 7},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };

    *opts = (struct options){
        .brightness = 0.0,
        .contrast = 1.0,
        .gamma = 1.0,
        .red = 1.0,
        .green = 1.0,
        .blue = 1.0,
    };

    for (;;) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "lo:h", long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'l':
            opts->list_only = true;
            break;
        case 'o':
            opts->output_selector = optarg;
            break;
        case 'h':
            usage(stdout, argv[0]);
            exit(0);
        case 1:
            opts->brightness = strtod(optarg, NULL);
            break;
        case 2:
            opts->contrast = strtod(optarg, NULL);
            break;
        case 3:
            opts->gamma = strtod(optarg, NULL);
            break;
        case 4:
            opts->red = strtod(optarg, NULL);
            break;
        case 5:
            opts->green = strtod(optarg, NULL);
            break;
        case 6:
            opts->blue = strtod(optarg, NULL);
            break;
        case 7:
            opts->reset = true;
            opts->brightness = 0.0;
            opts->contrast = 1.0;
            opts->gamma = 1.0;
            opts->red = 1.0;
            opts->green = 1.0;
            opts->blue = 1.0;
            break;
        default:
            usage(stderr, argv[0]);
            exit(2);
        }
    }
}

static void free_output(struct output *output) {
    if (output->wl_output) {
        wl_output_destroy(output->wl_output);
    }
    free(output->make);
    free(output->model);
    free(output->name);
    free(output->description);
}

static void destroy_app_state(struct app_state *app) {
    if (!app) {
        return;
    }

    if (app->outputs) {
        for (size_t i = 0; i < app->output_count; ++i) {
            free_output(&app->outputs[i]);
        }
        free(app->outputs);
    }

    if (app->gamma_manager) {
        zwlr_gamma_control_manager_v1_destroy(app->gamma_manager);
    }
    if (app->registry) {
        wl_registry_destroy(app->registry);
    }
    if (app->display) {
        wl_display_disconnect(app->display);
    }
}

static void output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t phys_width, int32_t phys_height,
    int32_t subpixel, const char *make, const char *model, int32_t transform) {
    (void)wl_output;
    (void)x;
    (void)y;
    (void)phys_width;
    (void)phys_height;
    (void)subpixel;
    (void)transform;

    struct output *output = data;
    free(output->make);
    free(output->model);
    output->make = xstrdup(make);
    output->model = xstrdup(model);
}

static void output_mode(void *data, struct wl_output *wl_output,
    uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    (void)wl_output;
    struct output *output = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        output->width = width;
        output->height = height;
        output->refresh = refresh;
    }
}

static void output_done(void *data, struct wl_output *wl_output) {
    (void)wl_output;
    struct output *output = data;
    output->done = true;
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    (void)wl_output;
    struct output *output = data;
    output->scale = factor;
}

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)wl_output;
    struct output *output = data;
    free(output->name);
    output->name = xstrdup(name);
}

static void output_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)wl_output;
    struct output *output = data;
    free(output->description);
    output->description = xstrdup(description);
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static struct output *append_output(struct app_state *app) {
    size_t next_count = app->output_count + 1;
    struct output *new_outputs = realloc(app->outputs, next_count * sizeof(*new_outputs));
    if (!new_outputs) {
        perror("realloc");
        exit(1);
    }
    app->outputs = new_outputs;
    struct output *output = &app->outputs[app->output_count];
    *output = (struct output){0};
    output->scale = 1;
    app->output_count = next_count;
    return output;
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version) {
    struct app_state *app = data;
    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct output *output = append_output(app);
        uint32_t bind_version = version < 4 ? version : 4;
        output->registry_name = name;
        output->global_name = (uint32_t)(app->output_count - 1);
        output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, bind_version);
        wl_output_add_listener(output->wl_output, &output_listener, output);
    } else if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        uint32_t bind_version = version < 1 ? version : 1;
        app->gamma_manager = wl_registry_bind(
            registry, name, &zwlr_gamma_control_manager_v1_interface, bind_version);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void gamma_size_handler(void *data, struct zwlr_gamma_control_v1 *control, uint32_t size) {
    (void)control;
    struct gamma_request *request = data;
    request->gamma_size = size;
    request->ready = true;
}

static void gamma_failed_handler(void *data, struct zwlr_gamma_control_v1 *control) {
    (void)control;
    struct gamma_request *request = data;
    request->failed = true;
    request->ready = true;
}

static const struct zwlr_gamma_control_v1_listener gamma_listener = {
    .gamma_size = gamma_size_handler,
    .failed = gamma_failed_handler,
};

static void signal_handler(int signo) {
    (void)signo;
    g_should_exit = 1;
}

static int connect_wayland(struct app_state *app) {
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return -1;
    }

    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);

    if (wl_display_roundtrip(app->display) < 0) {
        fprintf(stderr, "Wayland roundtrip failed\n");
        return -1;
    }
    if (wl_display_roundtrip(app->display) < 0) {
        fprintf(stderr, "Wayland output roundtrip failed\n");
        return -1;
    }

    return 0;
}

static void print_output_summary(const struct output *output, size_t index) {
    const char *name = output->name ? output->name : "(unnamed)";
    const char *desc = output->description ? output->description : "";
    printf("[%zu] %s", index, name);
    if (desc[0] != '\0') {
        printf(" - %s", desc);
    } else if (output->make || output->model) {
        printf(" - %s%s%s",
            output->make ? output->make : "",
            (output->make && output->model) ? " " : "",
            output->model ? output->model : "");
    }
    if (output->width > 0 && output->height > 0) {
        printf(" (%dx%d", output->width, output->height);
        if (output->refresh > 0) {
            printf("@%.2fHz", output->refresh / 1000.0);
        }
        printf(", scale %d)", output->scale);
    }
    putchar('\n');
}

static bool output_matches(const struct output *output, size_t index, const char *selector) {
    if (!selector) {
        return true;
    }

    char *endptr = NULL;
    errno = 0;
    unsigned long wanted_index = strtoul(selector, &endptr, 10);
    if (errno == 0 && endptr && *endptr == '\0') {
        return wanted_index == index;
    }

    if (output->name && strcmp(output->name, selector) == 0) {
        return true;
    }
    if (output->description && strcmp(output->description, selector) == 0) {
        return true;
    }

    return false;
}

static double clamp(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static uint16_t encode_sample(double normalized) {
    normalized = clamp(normalized, 0.0, 1.0);
    return (uint16_t)lrint(normalized * 65535.0);
}

static int create_anonymous_file(size_t size) {
    char template[] = "/tmp/waycolorctl-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }
    unlink(template);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void fill_gamma_ramp(uint16_t *ramps, uint32_t gamma_size, const struct options *opts) {
    double channel_gain[3] = {opts->red, opts->green, opts->blue};
    for (uint32_t i = 0; i < gamma_size; ++i) {
        double x = gamma_size == 1 ? 1.0 : (double)i / (double)(gamma_size - 1);
        for (size_t channel = 0; channel < 3; ++channel) {
            double y = x;
            y = ((y - 0.5) * opts->contrast) + 0.5 + opts->brightness;
            y = clamp(y, 0.0, 1.0);
            y = pow(y, 1.0 / opts->gamma);
            y *= channel_gain[channel];
            ramps[channel * gamma_size + i] = encode_sample(y);
        }
    }
}

static int apply_to_output(
    struct app_state *app, struct gamma_request *request, const struct options *opts) {
    request->control = zwlr_gamma_control_manager_v1_get_gamma_control(
        app->gamma_manager, request->output->wl_output);
    zwlr_gamma_control_v1_add_listener(request->control, &gamma_listener, request);

    if (wl_display_roundtrip(app->display) < 0) {
        fprintf(stderr, "Wayland roundtrip failed while waiting for gamma size\n");
        zwlr_gamma_control_v1_destroy(request->control);
        request->control = NULL;
        return -1;
    }

    if (request->failed || request->gamma_size == 0) {
        fprintf(stderr, "Failed to acquire gamma control for output %s\n",
            request->output->name ? request->output->name : "(unnamed)");
        if (request->control) {
            zwlr_gamma_control_v1_destroy(request->control);
            request->control = NULL;
        }
        return -1;
    }

    size_t table_len = request->gamma_size * 3u * sizeof(uint16_t);
    int fd = create_anonymous_file(table_len);
    if (fd < 0) {
        perror("mkstemp/ftruncate");
        zwlr_gamma_control_v1_destroy(request->control);
        request->control = NULL;
        return -1;
    }

    uint16_t *ramps = mmap(NULL, table_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ramps == MAP_FAILED) {
        perror("mmap");
        close(fd);
        zwlr_gamma_control_v1_destroy(request->control);
        request->control = NULL;
        return -1;
    }

    fill_gamma_ramp(ramps, request->gamma_size, opts);
    msync(ramps, table_len, MS_SYNC);
    munmap(ramps, table_len);

    zwlr_gamma_control_v1_set_gamma(request->control, fd);
    close(fd);

    if (wl_display_roundtrip(app->display) < 0) {
        fprintf(stderr, "Wayland roundtrip failed while applying gamma ramp\n");
        zwlr_gamma_control_v1_destroy(request->control);
        request->control = NULL;
        return -1;
    }

    if (request->failed) {
        fprintf(stderr, "Compositor rejected gamma ramp for output %s\n",
            request->output->name ? request->output->name : "(unnamed)");
        zwlr_gamma_control_v1_destroy(request->control);
        request->control = NULL;
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    struct options opts;
    parse_options(argc, argv, &opts);

    struct app_state app = {0};
    if (connect_wayland(&app) < 0) {
        destroy_app_state(&app);
        return 1;
    }

    if (app.output_count == 0) {
        fprintf(stderr, "No Wayland outputs found\n");
        destroy_app_state(&app);
        return 1;
    }

    if (opts.list_only) {
        for (size_t i = 0; i < app.output_count; ++i) {
            print_output_summary(&app.outputs[i], i);
        }
        destroy_app_state(&app);
        return 0;
    }

    if (!app.gamma_manager) {
        fprintf(stderr,
            "Compositor does not expose zwlr_gamma_control_manager_v1. "
            "This tool needs wlroots gamma-control support.\n");
        destroy_app_state(&app);
        return 1;
    }

    struct sigaction sa = {.sa_handler = signal_handler};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct gamma_request *active_requests = calloc(app.output_count, sizeof(*active_requests));
    if (!active_requests) {
        perror("calloc");
        destroy_app_state(&app);
        return 1;
    }

    size_t matched = 0;
    int status = 0;
    for (size_t i = 0; i < app.output_count; ++i) {
        struct output *output = &app.outputs[i];
        if (!output_matches(output, i, opts.output_selector)) {
            continue;
        }

        struct gamma_request *request = &active_requests[matched];
        *request = (struct gamma_request){.output = output};
        ++matched;
        if (apply_to_output(&app, request, &opts) < 0) {
            status = 1;
        } else {
            printf("Applied to %s\n", output->name ? output->name : "(unnamed)");
        }
    }

    if (matched == 0) {
        fprintf(stderr, "No output matched '%s'\n", opts.output_selector);
        status = 1;
    }

    if (status == 0) {
        fprintf(stderr, "waycolorctl is holding gamma control. Press Ctrl-C to restore defaults.\n");
        while (!g_should_exit) {
            int rc = wl_display_dispatch(app.display);
            if (rc < 0) {
                perror("wl_display_dispatch");
                status = 1;
                break;
            }
        }
    }

    for (size_t i = 0; i < matched; ++i) {
        if (active_requests[i].control) {
            zwlr_gamma_control_v1_destroy(active_requests[i].control);
        }
    }
    free(active_requests);
    destroy_app_state(&app);
    return status;
}
