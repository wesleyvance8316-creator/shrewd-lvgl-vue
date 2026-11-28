/**
 * @file sdl_port.h
 * @brief 桌面演示用的显示 + 输入接入(SDL2)。
 *
 * **框架不碰 SDL** —— `shrewd-lv-view` 只认 `lv_display_t` 和 `lv_indev_t`,
 * 窗口、像素怎么搬、事件从哪来都归应用。这正是嵌入式上接自己的屏和触摸时
 * 要写的那层,所以它放在 demo 里当**怎么接显示/输入的样本**,不进框架。
 *
 * 要换到板子上:把 SDL 换成 fbdev + evdev(或你的屏驱),`lv_display_*` 与
 * `lv_indev_*` 这几行不用动。
 */
#ifndef DEMO_SDL_PORT_H
#define DEMO_SDL_PORT_H

#include <stdint.h>

#include "lvgl/lvgl.h"

typedef struct {
    void *window; /* SDL_Window *   —— 头文件不暴露 SDL 类型 */
    void *renderer;
    void *texture;
    lv_display_t *display;
    lv_indev_t *pointer;
    lv_point_t point;
    lv_indev_state_t state;
    int quit;
} sdl_port_t;

/**
 * 开窗口、接上 LVGL 的 display 与指针 indev。
 * @return 0 成功;非 0 失败(SDL 没起来 / 窗口开不了)。
 */
int sdl_port_init(sdl_port_t *p, int32_t hor_res, int32_t ver_res, const char *title);

/** 把 SDL 事件搬进 LVGL 的输入状态。每帧调一次。 */
void sdl_port_poll(sdl_port_t *p);

/** 用户关窗口或按 ESC。 */
int sdl_port_wants_quit(const sdl_port_t *p);

/** 帧节奏。真板子上换成你的主循环节奏。 */
void sdl_port_delay(unsigned int ms);

void sdl_port_dispose(sdl_port_t *p);

#endif /* DEMO_SDL_PORT_H */
