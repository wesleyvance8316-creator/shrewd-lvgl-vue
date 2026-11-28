/**
 * @file sdl_port.c
 * @brief SDL2 显示 + 指针输入的接入实现。
 *
 * 全部走 LVGL 的公开接口(`lv_display_*` / `lv_indev_*`),所以换到板子上只是
 * 把这里的 SDL 换成 fbdev/evdev,外面的应用代码一行不用改。
 */
#include "platform/sdl_port.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

/** 显示用 FULL 模式:一帧一整块缓冲,flush 就是整张贴图 —— 最好懂的一种。 */
static void sdl_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    sdl_port_t *p = (sdl_port_t *)lv_display_get_user_data(disp);
    (void)area;
    if ((NULL == p) || (NULL == p->texture)) {
        lv_display_flush_ready(disp);
        return;
    }
    int32_t w = lv_display_get_horizontal_resolution(disp);
    SDL_UpdateTexture((SDL_Texture *)p->texture, NULL, px_map, (int)(w * 4));
    SDL_RenderClear((SDL_Renderer *)p->renderer);
    SDL_RenderCopy((SDL_Renderer *)p->renderer, (SDL_Texture *)p->texture, NULL, NULL);
    SDL_RenderPresent((SDL_Renderer *)p->renderer);
    lv_display_flush_ready(disp);
}

static void sdl_pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
    sdl_port_t *p = (sdl_port_t *)lv_indev_get_user_data(indev);
    if (NULL == p) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point = p->point;
    data->state = p->state;
}

int sdl_port_init(sdl_port_t *p, int32_t hor_res, int32_t ver_res, const char *title) {
    if (NULL == p) {
        return -1;
    }
    memset(p, 0, sizeof(*p));
    if (0 != SDL_Init(SDL_INIT_VIDEO)) {
        return -1;
    }
    SDL_Window *window = SDL_CreateWindow((NULL != title) ? title : "shrewd-lv-view",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          (int)hor_res,
                                          (int)ver_res,
                                          SDL_WINDOW_SHOWN);
    if (NULL == window) {
        SDL_Quit();
        return -1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture *texture = (NULL != renderer) ? SDL_CreateTexture(renderer,
                                                                  SDL_PIXELFORMAT_ARGB8888,
                                                                  SDL_TEXTUREACCESS_STREAMING,
                                                                  (int)hor_res,
                                                                  (int)ver_res)
                                              : NULL;
    if ((NULL == renderer) || (NULL == texture)) {
        if (NULL != texture) {
            SDL_DestroyTexture(texture);
        }
        if (NULL != renderer) {
            SDL_DestroyRenderer(renderer);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    p->window = window;
    p->renderer = renderer;
    p->texture = texture;

    /* 显示:一块全屏缓冲 + FULL 渲染模式。 */
    static uint8_t *s_buf;
    size_t bytes = (size_t)hor_res * (size_t)ver_res * 4u;
    s_buf = (uint8_t *)calloc(1, bytes);
    if (NULL == s_buf) {
        sdl_port_dispose(p);
        return -1;
    }
    lv_display_t *disp = lv_display_create(hor_res, ver_res);
    lv_display_set_user_data(disp, p);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(disp, sdl_flush);
    lv_display_set_buffers(disp, s_buf, NULL, (uint32_t)bytes, LV_DISPLAY_RENDER_MODE_FULL);
    p->display = disp;

    /* 指针:鼠标当触摸。 */
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, sdl_pointer_read);
    lv_indev_set_user_data(pointer, p);
    lv_indev_set_display(pointer, disp);
    p->pointer = pointer;
    p->state = LV_INDEV_STATE_RELEASED;
    return 0;
}

void sdl_port_poll(sdl_port_t *p) {
    if (NULL == p) {
        return;
    }
    SDL_Event e;
    while (0 != SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            p->quit = 1;
            break;
        case SDL_KEYDOWN:
            if (SDLK_ESCAPE == e.key.keysym.sym) {
                p->quit = 1;
            }
            break;
        case SDL_MOUSEMOTION:
            p->point.x = (int32_t)e.motion.x;
            p->point.y = (int32_t)e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            p->point.x = (int32_t)e.button.x;
            p->point.y = (int32_t)e.button.y;
            p->state = LV_INDEV_STATE_PRESSED;
            break;
        case SDL_MOUSEBUTTONUP:
            p->point.x = (int32_t)e.button.x;
            p->point.y = (int32_t)e.button.y;
            p->state = LV_INDEV_STATE_RELEASED;
            break;
        default:
            break;
        }
    }
}

int sdl_port_wants_quit(const sdl_port_t *p) {
    return (NULL != p) ? p->quit : 1;
}

void sdl_port_delay(unsigned int ms) {
    SDL_Delay(ms);
}

void sdl_port_dispose(sdl_port_t *p) {
    if (NULL == p) {
        return;
    }
    if (NULL != p->texture) {
        SDL_DestroyTexture((SDL_Texture *)p->texture);
    }
    if (NULL != p->renderer) {
        SDL_DestroyRenderer((SDL_Renderer *)p->renderer);
    }
    if (NULL != p->window) {
        SDL_DestroyWindow((SDL_Window *)p->window);
    }
    p->texture = NULL;
    p->renderer = NULL;
    p->window = NULL;
    SDL_Quit();
}
