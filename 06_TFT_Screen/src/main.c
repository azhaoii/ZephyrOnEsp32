#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/input/input.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/sys/time.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/unistd.h>

#include <string.h>
#include <stdlib.h>

#define WIDTH  480
#define HEIGHT 320

/* 分屏带渲染：每次只渲染 BAND_H 行，避免 480x320 整帧缓冲（300KB）超内存 */
#define BAND_H     80
#define NUM_BANDS  (HEIGHT / BAND_H)

#define HTTP_HOST "api.open-meteo.com"
#define HTTP_PORT "80"
#define HTTP_PATH "/v1/forecast?latitude=31.4928&longitude=120.2622&current=temperature_2m,relative_humidity_2m"
#define FETCH_PERIOD_MS  60000
#define WIFI_RETRY_MS    3000

static const struct gpio_dt_spec bl = GPIO_DT_SPEC_GET(DT_ALIAS(bl_led), gpios);
static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint16_t fb[WIDTH * BAND_H];
static int band_y0;

static atomic_t touch_x = ATOMIC_INIT(-1);
static atomic_t touch_y = ATOMIC_INIT(-1);
static atomic_t touch_pressed = ATOMIC_INIT(0);

static int last_tx = -2;
static int last_ty = -2;
static bool last_tp;

/* font5x7: 64 glyphs, index = ch - ' ' */
static const uint8_t font[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*  */
    {0x00,0x00,0x5f,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7f,0x14,0x7f,0x14}, /* # */
    {0x24,0x2a,0x7f,0x2a,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1c,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1c,0x00}, /* ) */
    {0x14,0x08,0x3e,0x08,0x14}, /* * */
    {0x08,0x08,0x3e,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3e,0x51,0x49,0x45,0x3e}, /* 0 */
    {0x00,0x42,0x7f,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4b,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7f,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3c,0x4a,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1e}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3e}, /* @ */
    {0x7e,0x11,0x11,0x11,0x7e}, /* A */
    {0x7f,0x49,0x49,0x49,0x36}, /* B */
    {0x3e,0x41,0x41,0x41,0x22}, /* C */
    {0x7f,0x41,0x41,0x22,0x1c}, /* D */
    {0x7f,0x49,0x49,0x49,0x41}, /* E */
    {0x7f,0x09,0x09,0x09,0x01}, /* F */
    {0x3e,0x41,0x49,0x49,0x7a}, /* G */
    {0x7f,0x08,0x08,0x08,0x7f}, /* H */
    {0x00,0x41,0x7f,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3f,0x01}, /* J */
    {0x7f,0x08,0x14,0x22,0x41}, /* K */
    {0x7f,0x40,0x40,0x40,0x40}, /* L */
    {0x7f,0x02,0x0c,0x02,0x7f}, /* M */
    {0x7f,0x04,0x08,0x10,0x7f}, /* N */
    {0x3e,0x41,0x41,0x41,0x3e}, /* O */
    {0x7f,0x09,0x09,0x09,0x06}, /* P */
    {0x3e,0x41,0x51,0x21,0x5e}, /* Q */
    {0x7f,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7f,0x01,0x01}, /* T */
    {0x3f,0x40,0x40,0x40,0x3f}, /* U */
    {0x1f,0x20,0x40,0x20,0x1f}, /* V */
    {0x3f,0x40,0x38,0x40,0x3f}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7f,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x00,0x41,0x41,0x7f,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
};

static void px(int x, int y, uint16_t c)
{
    if (x >= 0 && x < WIDTH && y >= band_y0 && y < band_y0 + BAND_H) {
        fb[(y - band_y0) * WIDTH + x] = c;
    }
}

static void rect_fill(int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            px(i, j, c);
        }
    }
}

static void disc(int cx, int cy, int r, uint16_t c)
{
    for (int j = cy - r; j <= cy + r; j++) {
        for (int i = cx - r; i <= cx + r; i++) {
            if ((i - cx) * (i - cx) + (j - cy) * (j - cy) <= r * r) {
                px(i, j, c);
            }
        }
    }
}

static void draw_char(int x, int y, char ch, uint16_t c)
{
    int idx = ch - ' ';
    if (idx < 0 || idx >= (int)ARRAY_SIZE(font)) {
        return;
    }
    for (int i = 0; i < 5; i++) {
        uint8_t bits = font[idx][i];
        for (int j = 0; j < 7; j++) {
            if (bits & (0x01 << j)) {
                px(x + i, y + j, c);
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++) {
        draw_char(x, y, *s, c);
        x += 6;
    }
}

static void draw_char_big(int x, int y, char ch, int scale, uint16_t c)
{
    int idx = ch - ' ';
    if (idx < 0 || idx >= (int)ARRAY_SIZE(font)) {
        return;
    }
    for (int i = 0; i < 5; i++) {
        uint8_t bits = font[idx][i];
        for (int j = 0; j < 7; j++) {
            if (bits & (0x01 << j)) {
                rect_fill(x + i * scale, y + j * scale, scale, scale, c);
            }
        }
    }
}

static void draw_text_big(int x, int y, const char *s, int scale, uint16_t c)
{
    for (; *s; s++) {
        draw_char_big(x, y, *s, scale, c);
        x += 6 * scale;
    }
}

/* 沿矩形边框顺时针走 d/360（0..360 对应 0..100%） */
static void ring_walk(int d, int x0, int y0, int top, int side, uint16_t c)
{
    if (d <= 0) {
        return;
    }
    if (d < top) {
        rect_fill(x0, y0, d, 2, c);
    } else if (d < top + side) {
        rect_fill(x0, y0, top, 2, c);
        rect_fill(x0 + top - 2, y0, 2, d - top + 2, c);
    } else if (d < 2 * top + side) {
        rect_fill(x0, y0, top, 2, c);
        rect_fill(x0 + top - 2, y0, 2, side, c);
        rect_fill(x0, y0 + side - 2, d - top - side + 2, 2, c);
    } else {
        int rest = d - (2 * top + side);
        rect_fill(x0, y0, top, 2, c);
        rect_fill(x0 + top - 2, y0, 2, side, c);
        rect_fill(x0, y0 + side - 2, top, 2, c);
        rect_fill(x0, y0 + side - 2 - rest, 2, rest + 2, c);
    }
}

static void flush_fb(void)
{
    static const struct display_buffer_descriptor desc = {
        .buf_size = sizeof(fb),
        .width = WIDTH,
        .height = BAND_H,
        .pitch = WIDTH,
    };
    display_write(display, 0, band_y0, &desc, fb);
}

static bool get_number(const char *buf, const char *key, double *out)
{
    char needle[24];
    char *p;
    char *end;

    snprintk(needle, sizeof(needle), "\"%s\":", key);
    p = strstr(buf, needle);
    if (p == NULL) {
        return false;
    }
    p += strlen(needle);
    *out = strtod(p, &end);
    return end != p;
}

static bool http_get_weather(double *temp, double *hum)
{
    struct addrinfo hints = { 0 };
    struct addrinfo *res = NULL;
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    static char buf[4096];
    char req[512];
    int sock = -1;
    int slen = 0;
    bool ret = false;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(HTTP_HOST, HTTP_PORT, &hints, &res) != 0) {
        return false;
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return false;
    }

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        goto out;
    }

    snprintk(req, sizeof(req),
             "GET " HTTP_PATH " HTTP/1.1\r\n"
             "Host: " HTTP_HOST "\r\n"
             "User-Agent: zephyr-weather\r\n"
             "Connection: close\r\n\r\n");
    if (send(sock, req, strlen(req), 0) < 0) {
        goto out;
    }

    memset(buf, 0, sizeof(buf));
    while (slen < (int)sizeof(buf) - 1) {
        int r = recv(sock, buf + slen, sizeof(buf) - 1 - slen, 0);
        if (r <= 0) {
            break;
        }
        slen += r;
    }
    if (slen == 0) {
        goto out;
    }

    if (get_number(buf, "temperature_2m", temp) &&
        get_number(buf, "relative_humidity_2m", hum)) {
        ret = true;
    }

out:
    close(sock);
    freeaddrinfo(res);
    return ret;
}

static void draw_touch_overlay(void)
{
    int x = (int)atomic_get(&touch_x);
    int y = (int)atomic_get(&touch_y);
    bool pressed = (bool)atomic_get(&touch_pressed);

    if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) {
        return;
    }

    disc(x, y, pressed ? 5 : 3, pressed ? 0x07ff : 0x2d6b);
    rect_fill(x - 16, y - 1, 32, 2, 0xffff);
    rect_fill(x - 1, y - 16, 2, 32, 0xffff);
}

static void draw_frame(double temp, double hum, bool got, int countdown)
{
    char buf[32];
    char num[16];
    int rh;
    int tmp;
    int tp;

    memset(fb, 0, sizeof(fb));

    draw_text_big(10, 8, "WUXI BINHU", 2, 0xffff);
    draw_text(20, 30, "OPEN-METEO API", 0x4a49);

    if (got) {
        tmp = (int)temp;
        tp = (int)((temp - (double)tmp) * 10.0);
        if (tp < 0) {
            tp = -tp;
        }
        snprintk(num, sizeof(num), "%d.%d", tmp, tp);

        draw_text_big(10, 64, "TEMP", 2, 0xffe0);
        draw_text_big(10, 96, num, 6, 0xffff);
        disc(10 + 6 * 6 * (int)strlen(num) + 18, 117, 15, 0xffff);
        draw_text(10 + 6 * 6 * (int)strlen(num) + 38, 114, "DEG C", 0xffe0);

        rh = (int)hum;
        if (rh < 0) {
            rh = 0;
        }
        if (rh > 100) {
            rh = 100;
        }
        snprintk(buf, sizeof(buf), "%d%%", rh);
        draw_text_big(290, 64, "HUM", 2, 0x07e0);
        draw_text_big(290, 96, buf, 6, 0x07e0);

        /* 湿度进度条 20..460 */
        rect_fill(20, 180, 440, 20, 0x0842);
        rect_fill(20, 180, ((rh * 440) / 100), 20, 0x06ff);
        rect_fill(20, 180, 440, 1, 0xffff);
        rect_fill(20, 180, 1, 20, 0xffff);
        rect_fill(459, 180, 1, 20, 0xffff);
        rect_fill(20, 199, 440, 1, 0xffff);

        snprintk(buf, sizeof(buf), "DATA OK  %.1f C / %d RH", temp, rh);
        draw_text(20, 248, buf, 0x07e0);
    } else {
        draw_text_big(20, 90, "WAITING", 3, 0xffe0);
        draw_text_big(20, 124, "NETWORK...", 3, 0xffe0);
    }

    if (countdown >= 0) {
        snprintk(buf, sizeof(buf), "NEXT  %3ds", countdown);
        draw_text(20, 222, buf, 0x4a49);
        ring_walk((int)(((uint64_t)countdown * 360) / (FETCH_PERIOD_MS / 1000)),
                  8, 240, 464, 24, 0x07e0);
    }

    /* 触摸回显 */
    snprintk(buf, sizeof(buf), "TOUCH (%d,%d) %s",
             (int)atomic_get(&touch_x), (int)atomic_get(&touch_y),
             atomic_get(&touch_pressed) ? "DOWN" : "UP");
    draw_text(8, 268, buf, 0x4a49);
    draw_touch_overlay();

    flush_fb();
}

static bool ipv4_ready(struct net_if *iface)
{
    return iface != NULL && net_if_is_up(iface) &&
           net_if_ipv4_get_global_addr(iface, 0) != NULL;
}

static void touch_evt_cb(struct input_event *evt, void *user_data)
{
    switch (evt->code) {
    case INPUT_ABS_X:
        atomic_set(&touch_x, evt->value);
        break;
    case INPUT_ABS_Y:
        atomic_set(&touch_y, evt->value);
        break;
    case INPUT_BTN_TOUCH:
        atomic_set(&touch_pressed, evt->value);
        break;
    default:
        break;
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(ft6336u)), touch_evt_cb, NULL);

static void draw_frame_all(double temp, double hum, bool got, int countdown)
{
    for (int band = 0; band < NUM_BANDS; band++) {
        band_y0 = band * BAND_H;
        draw_frame(temp, hum, got, countdown);
        flush_fb();
    }
    band_y0 = 0;
}

/* 触摸变化检测（用于快速重绘触摸区域） */
static bool touch_changed(void)
{
    int x = (int)atomic_get(&touch_x);
    int y = (int)atomic_get(&touch_y);
    bool p = (bool)atomic_get(&touch_pressed);

    if (x != last_tx || y != last_ty || p != last_tp) {
        last_tx = x;
        last_ty = y;
        last_tp = p;
        return true;
    }
    return false;
}

/* 只重绘触摸十字所在条带 + 状态条带（band 3），用于高频刷新 */
static void draw_touch_bands(double temp, double hum, bool got, int countdown)
{
    int by = (int)atomic_get(&touch_y);
    int tband = (by >= 0 && by < HEIGHT) ? (by / BAND_H) : -1;

    for (int band = 0; band < NUM_BANDS; band++) {
        if (band == 3 || band == tband) {
            band_y0 = band * BAND_H;
            draw_frame(temp, hum, got, countdown);
            flush_fb();
        }
    }
    band_y0 = 0;
}

int main(void)
{
    double temp = 0.0;
    double hum = 0.0;
    bool got = false;
    int64_t fetch_next = k_uptime_get();
    int64_t last_wifi_req = -WIFI_RETRY_MS;
    int64_t last_full = k_uptime_get();

    gpio_pin_configure_dt(&bl, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&bl, 1);
    display_blanking_off(display);

    draw_frame_all(temp, hum, false, -1);

    while (1) {
        int64_t now = k_uptime_get();
        struct net_if *iface = net_if_get_wifi_sta();

        if (!ipv4_ready(iface)) {
            if (iface != NULL && now - last_wifi_req >= WIFI_RETRY_MS) {
                net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
                last_wifi_req = now;
            }
            draw_frame_all(temp, hum, false, -1);
            k_msleep(200);
            continue;
        }

        if (now >= fetch_next) {
            got = http_get_weather(&temp, &hum);
            if (got) {
                printk("Weather OK: %.1f C, %d %%RH\n", temp, (int)hum);
            } else {
                printk("Weather fetch FAILED\n");
            }
            fetch_next = now + FETCH_PERIOD_MS;
            draw_frame_all(temp, hum, got, (int)((fetch_next - now) / 1000));
            k_msleep(30);
        } else if (now - last_full >= 1000) {
            last_full = now;
            draw_frame_all(temp, hum, got, (int)((fetch_next - now) / 1000));
            k_msleep(30);
        }

        /* 触摸移动时高频重绘触摸条带（其余条带不动，整屏仍 1Hz） */
        if (touch_changed()) {
            draw_touch_bands(temp, hum, got, (int)((fetch_next - now) / 1000));
        }
        k_msleep(30);
    }
}
