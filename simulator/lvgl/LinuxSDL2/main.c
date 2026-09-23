#define _DEFAULT_SOURCE
#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <SDL2/SDL.h>

#include "lvgl/lvgl.h"
#include "lv_drivers/display/monitor.h"
#include "lv_drivers/indev/mouse.h"
#include "lv_drivers/indev/keyboard.h"
#include "lv_drivers/indev/mousewheel.h"

extern void main_screen(void);
extern void ic_main_entry(void);

static void hal_init(void)
{
    static lv_disp_buf_t disp_buf;
    static lv_color_t buf[LV_HOR_RES_MAX * 20];
    lv_disp_drv_t disp_drv;
    lv_indev_drv_t indev_drv;
    lv_group_t *group;
    lv_indev_t *indev;

    monitor_init();

    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 20);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = MONITOR_HOR_RES;
    disp_drv.ver_res = MONITOR_VER_RES;
    disp_drv.flush_cb = monitor_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    group = lv_group_create();

    mouse_init();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    lv_indev_drv_register(&indev_drv);

    keyboard_init();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keyboard_read;
    indev = lv_indev_drv_register(&indev_drv);
    lv_indev_set_group(indev, group);

    mousewheel_init();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = mousewheel_read;
    indev = lv_indev_drv_register(&indev_drv);
    lv_indev_set_group(indev, group);
}

int main(void)
{
    printf("watch_sim %dx%d  zoom=%d  window=%dx%d\n",
           MONITOR_HOR_RES, MONITOR_VER_RES, MONITOR_ZOOM,
           MONITOR_HOR_RES * MONITOR_ZOOM, MONITOR_VER_RES * MONITOR_ZOOM);

    lv_init();
    hal_init();
    main_screen();
    ic_main_entry();

    while (1) {
        lv_task_handler();
        usleep(5000);
    }
    return 0;
}

uint32_t custom_tick_get(void)
{
    static uint64_t start_ms;
    struct timeval tv;
    uint64_t now_ms;

    gettimeofday(&tv, NULL);
    now_ms = (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
    if (start_ms == 0)
        start_ms = now_ms;
    return (uint32_t)(now_ms - start_ms);
}
