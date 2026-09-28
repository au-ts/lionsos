/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Calculator: a microui app. Use the buttons or type digits and + - * / = */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include "mu_app.h"
#include <stdlib.h>

#define WIDTH 320
#define HEIGHT 336
#define ENTRY_MAX 14

static char entry[ENTRY_MAX + 2] = "0";
static double acc;
static char op;
/* The next digit starts a new number */
static bool fresh = true;
static bool error;

static bool entry_has_point(void)
{
    for (const char *p = entry; *p != '\0'; p++) {
        if (*p == '.') {
            return true;
        }
    }
    return false;
}

static double entry_value(void)
{
    return strtod(entry, NULL);
}

static void show_value(double v)
{
    if (v != v || v > 1e15 || v < -1e15) {
        error = true;
        strcpy(entry, "Error");
        return;
    }

    /* Fixed point without trailing zeros, trimmed to fit the display */
    char buf[48];
    sddf_snprintf(buf, sizeof(buf), "%.8f", v);
    size_t len = strlen(buf);
    while (len > 0 && buf[len - 1] == '0') {
        len--;
    }
    if (len > 0 && buf[len - 1] == '.') {
        len--;
    }
    if (len > ENTRY_MAX) {
        len = ENTRY_MAX;
        if (buf[len - 1] == '.') {
            len--;
        }
    }
    buf[len] = '\0';
    if (strcmp(buf, "-0") == 0) {
        strcpy(buf, "0");
    }
    strcpy(entry, buf);
}

static void clear(void)
{
    strcpy(entry, "0");
    acc = 0;
    op = 0;
    fresh = true;
    error = false;
}

static void apply_pending(void)
{
    double v = entry_value();
    switch (op) {
    case '+':
        acc += v;
        break;
    case '-':
        acc -= v;
        break;
    case '*':
        acc *= v;
        break;
    case '/':
        if (v == 0) {
            error = true;
            strcpy(entry, "Error");
            return;
        }
        acc /= v;
        break;
    default:
        acc = v;
        break;
    }
    show_value(acc);
}

static void press(char key)
{
    if (error && key != 'C') {
        return;
    }

    if ((key >= '0' && key <= '9') || key == '.') {
        if (fresh) {
            strcpy(entry, "0");
            fresh = false;
        }
        size_t len = strlen(entry);
        if (key == '.' && entry_has_point()) {
            return;
        }
        if (len >= ENTRY_MAX) {
            return;
        }
        if (strcmp(entry, "0") == 0 && key != '.') {
            len = 0;
        }
        entry[len] = key;
        entry[len + 1] = '\0';
        return;
    }

    switch (key) {
    case 'C':
        clear();
        break;
    case 'b': /* backspace */
        if (!fresh) {
            size_t len = strlen(entry);
            if (len > 1 && !(len == 2 && entry[0] == '-')) {
                entry[len - 1] = '\0';
            } else {
                strcpy(entry, "0");
            }
        }
        break;
    case 'n': /* negate */
        show_value(-entry_value());
        break;
    case '%':
        show_value(entry_value() / 100.0);
        break;
    case '+':
    case '-':
    case '*':
    case '/':
        if (!fresh || op == 0) {
            apply_pending();
        }
        op = key;
        fresh = true;
        break;
    case '=':
        apply_pending();
        op = 0;
        fresh = true;
        break;
    }
}

static void on_char(char c)
{
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '*' || c == '/' || c == '='
        || c == '%') {
        press(c);
    } else if (c == 'c' || c == 'C') {
        press('C');
    }
}

static void frame(mu_Context *ctx)
{
    if (ctx->key_pressed & MU_KEY_RETURN) {
        press('=');
    }
    if (ctx->key_pressed & MU_KEY_BACKSPACE) {
        press('b');
    }

    /* Display */
    mu_layout_row(ctx, 1, (int[]) { -1 }, 44);
    mu_Rect r = mu_layout_next(ctx);
    mu_draw_rect(ctx, r, mu_color(0x1b, 0x1f, 0x27, 0xff));
    char display[ENTRY_MAX + 4];
    sddf_snprintf(display, sizeof(display), "%s %c", entry, op ? op : ' ');
    mu_draw_control_text(ctx, display, mu_rect(r.x, r.y, r.w - 8, r.h), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);

    static const char *const keys[5][4] = {
        { "C", "+/-", "%", "/" },
        { "7", "8", "9", "*" },
        { "4", "5", "6", "-" },
        { "1", "2", "3", "+" },
        { "0", ".", "<-", "=" },
    };
    int w = (WIDTH - 2 * ctx->style->padding - 3 * ctx->style->spacing) / 4;
    for (int row = 0; row < 5; row++) {
        mu_layout_row(ctx, 4, (int[]) { w, w, w, w }, 44);
        for (int col = 0; col < 4; col++) {
            const char *label = keys[row][col];
            if (mu_button(ctx, label)) {
                if (strcmp(label, "+/-") == 0) {
                    press('n');
                } else if (strcmp(label, "<-") == 0) {
                    press('b');
                } else {
                    press(label[0]);
                }
            }
        }
    }
}

void init(void)
{
    mu_app_init("Calculator", WIDTH, HEIGHT, frame, on_char);
}

void notified(microkit_channel ch)
{
    if (ch == GUI_COMPOSITOR_CH) {
        mu_app_handle_events();
    }
}
