#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/sem.h"

#include "FreeRTOS.h" /* Must come first. */
#include "task.h"     /* RTOS task related API prototypes. */
#include "queue.h"    /* RTOS queue related API prototypes. */
#include "timers.h"   /* Software timer related API prototypes. */
#include "semphr.h"   /* Semaphore related API prototypes. */

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

// 移除了keypad demo相关的包含文件
// #include "keypad_encoder/lv_demo_keypad_encoder.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"

#include "ws2812.pio.h"

void vApplicationTickHook(void)
{
    lv_tick_inc(1);
}

lv_obj_t *img1 = NULL; // 开机图片

lv_obj_t *led1 = NULL;
lv_obj_t *led2 = NULL;

lv_obj_t *jy_label = NULL;
lv_obj_t *joystick_circle = NULL;  // 摇杆外圆
lv_obj_t *joystick_ball = NULL;    // 摇杆内球

uint8_t adc_en = 0;

// 计算器相关变量
lv_obj_t *calc_display = NULL;
char calc_buffer[32] = "0";
double calc_num1 = 0;
double calc_num2 = 0;
char calc_operator = 0;
uint8_t calc_new_number = 1;

// 计算器按钮处理函数
static void calc_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *btn = lv_event_get_target(e);
        const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
        
        if (txt[0] >= '0' && txt[0] <= '9') {
            // 数字按钮
            if (calc_new_number) {
                calc_buffer[0] = txt[0];
                calc_buffer[1] = '\0';
                calc_new_number = 0;
            } else {
                if (strlen(calc_buffer) < 15) {
                    strcat(calc_buffer, txt);
                }
            }
        } else if (txt[0] == '.') {
            // 小数点
            if (strchr(calc_buffer, '.') == NULL && strlen(calc_buffer) < 15) {
                strcat(calc_buffer, ".");
            }
        } else if (txt[0] == 'C') {
            // 清除
            strcpy(calc_buffer, "0");
            calc_num1 = 0;
            calc_num2 = 0;
            calc_operator = 0;
            calc_new_number = 1;
        } else if (txt[0] == '=') {
            // 等于
            if (calc_operator) {
                calc_num2 = atof(calc_buffer);
                switch (calc_operator) {
                    case '+': calc_num1 = calc_num1 + calc_num2; break;
                    case '-': calc_num1 = calc_num1 - calc_num2; break;
                    case '*': calc_num1 = calc_num1 * calc_num2; break;
                    case '/': if (calc_num2 != 0) calc_num1 = calc_num1 / calc_num2; break;
                }
                snprintf(calc_buffer, 32, "%.6g", calc_num1);
                calc_operator = 0;
                calc_new_number = 1;
            }
        } else {
            // 运算符
            if (calc_operator && !calc_new_number) {
                calc_num2 = atof(calc_buffer);
                switch (calc_operator) {
                    case '+': calc_num1 = calc_num1 + calc_num2; break;
                    case '-': calc_num1 = calc_num1 - calc_num2; break;
                    case '*': calc_num1 = calc_num1 * calc_num2; break;
                    case '/': if (calc_num2 != 0) calc_num1 = calc_num1 / calc_num2; break;
                }
                snprintf(calc_buffer, 32, "%.6g", calc_num1);
            } else {
                calc_num1 = atof(calc_buffer);
            }
            calc_operator = txt[0];
            calc_new_number = 1;
        }
        
        lv_label_set_text(calc_display, calc_buffer);
    }
}

static void calculator_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        lv_obj_del(img1);
        lv_obj_clean(lv_scr_act());
        vTaskDelay(100 / portTICK_PERIOD_MS);
        
        // 创建显示屏
        calc_display = lv_label_create(lv_scr_act());
        lv_label_set_text(calc_display, "0");
        lv_obj_set_style_text_font(calc_display, &lv_font_montserrat_16, 0);  // 使用16号字体（配置中已启用）
        //lv_obj_set_style_text_color(calc_display, lv_color_white(), 0);  // 白色文字
        lv_obj_set_style_text_align(calc_display, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(calc_display, 300);
        lv_obj_align(calc_display, LV_ALIGN_TOP_MID, 0, 20);
        
        // 按钮布局：4x4网格 + 底部等号
        const char *btnm_map[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", ".", "+"
        };
        
        int btn_w = 70;
        int btn_h = 60;
        int start_x = 10;
        int start_y = 80;
        int gap = 10;
        
        // 创建4x4的按钮网格
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                int idx = row * 4 + col;
                
                lv_obj_t *btn = lv_btn_create(lv_scr_act());
                lv_obj_set_size(btn, btn_w, btn_h);
                lv_obj_set_pos(btn, start_x + col * (btn_w + gap), start_y + row * (btn_h + gap));
                lv_obj_add_event_cb(btn, calc_btn_event_handler, LV_EVENT_ALL, NULL);
                
                lv_obj_t *label = lv_label_create(btn);
                lv_label_set_text(label, btnm_map[idx]);
                lv_obj_center(label);
                lv_obj_set_style_text_color(label, lv_color_black(), 0);  // 设置文字为黑色
                
                // 数字按钮白色，运算符灰色
                if (btnm_map[idx][0] >= '0' && btnm_map[idx][0] <= '9') {
                    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
                } else if (btnm_map[idx][0] == '.') {
                    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
                } else {
                    lv_obj_set_style_bg_color(btn, lv_color_make(200, 200, 200), 0);
                }
            }
        }
        
        // = 按钮占两列
        lv_obj_t *btn_eq = lv_btn_create(lv_scr_act());
        lv_obj_set_size(btn_eq, btn_w * 4 + gap * 3, btn_h);
        lv_obj_set_pos(btn_eq, start_x, start_y + 4 * (btn_h + gap));
        lv_obj_add_event_cb(btn_eq, calc_btn_event_handler, LV_EVENT_ALL, NULL);
        lv_obj_set_style_bg_color(btn_eq, lv_color_make(100, 200, 100), 0);
        
        lv_obj_t *label_eq = lv_label_create(btn_eq);
        lv_label_set_text(label_eq, "=");
        lv_obj_center(label_eq);
        lv_obj_set_style_text_color(label_eq, lv_color_black(), 0);  // 设置文字为黑色
    }
}

static void beep_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED)
    {
        gpio_xor_mask(0x2000);
    }
}

static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) |
           ((uint32_t)(g) << 16) |
           (uint32_t)(b);
}

static void slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        lv_color_t color = lv_colorwheel_get_rgb(obj);
        put_pixel(urgb_u32(color.ch.red << 5, ((color.ch.green_h << 2) + color.ch.green_h) << 2, (color.ch.blue << 3)));
    }
}

static void clr_rgb_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        put_pixel(urgb_u32(0, 0, 0));
    }
}

void gpio_callback(uint gpio, uint32_t events)
{
    switch (gpio)
    {
    case 15:
        lv_led_toggle(led1);
        gpio_xor_mask(1ul << 16);
        break;
    case 14:
        lv_led_toggle(led2);
        gpio_xor_mask(1ul << 17);
        break;
    }
}

static void hw_handler(lv_event_t *e)
{
    lv_obj_t *label;

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        lv_obj_del(img1);
        lv_obj_clean(lv_scr_act());
        vTaskDelay(100 / portTICK_PERIOD_MS);

        // 蜂鸣器
        gpio_init(13);
        gpio_set_dir(13, GPIO_OUT);

        lv_obj_t *beep_btn = lv_btn_create(lv_scr_act());
        lv_obj_add_event_cb(beep_btn, beep_handler, LV_EVENT_ALL, NULL);
        lv_obj_align(beep_btn, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_add_flag(beep_btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_height(beep_btn, LV_SIZE_CONTENT);

        label = lv_label_create(beep_btn);
        lv_label_set_text(label, "Beep");
        lv_obj_center(label);

        // 清除颜色
        lv_obj_t *clr_rgb_btn = lv_btn_create(lv_scr_act());
        lv_obj_add_event_cb(clr_rgb_btn, clr_rgb_handler, LV_EVENT_ALL, NULL);
        lv_obj_align(clr_rgb_btn, LV_ALIGN_TOP_MID, 0, 80);

        label = lv_label_create(clr_rgb_btn);
        lv_label_set_text(label, "Turn off RGB");
        lv_obj_center(label);

        // RGB 灯

        /*Create a slider in the center of the display*/
        lv_obj_t *lv_colorwheel = lv_colorwheel_create(lv_scr_act(), true);
        lv_obj_set_size(lv_colorwheel, 200, 200);
        lv_obj_align(lv_colorwheel, LV_ALIGN_TOP_MID, 100, 0);

        lv_obj_center(lv_colorwheel);
        lv_obj_add_event_cb(lv_colorwheel, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        // todo get free sm
        PIO pio = pio0;
        int sm = 0;
        uint offset = pio_add_program(pio, &ws2812_program);

        ws2812_program_init(pio, sm, offset, 12, 800000, true);
        put_pixel(urgb_u32(0, 0, 0));

        gpio_set_irq_enabled_with_callback(14, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
        gpio_set_irq_enabled_with_callback(15, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
        gpio_set_irq_enabled_with_callback(22, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

        led1 = lv_led_create(lv_scr_act());
        lv_obj_align(led1, LV_ALIGN_TOP_MID, -30, 400);
        lv_led_set_color(led1, lv_palette_main(LV_PALETTE_GREEN));

        lv_led_off(led1);

        led2 = lv_led_create(lv_scr_act());
        lv_obj_align(led2, LV_ALIGN_TOP_MID, 30, 400);
        lv_led_set_color(led2, lv_palette_main(LV_PALETTE_BLUE));

        lv_led_off(led2);

        gpio_init(16);
        gpio_init(17);

        gpio_set_dir(16, GPIO_OUT);
        gpio_set_dir(17, GPIO_OUT);

        gpio_put(16, 0);
        gpio_put(17, 0);

        adc_en = 1;

        // 简单的摇杆显示 - 考虑Pico性能
        // jy_label = lv_label_create(lv_scr_act());
        // lv_label_set_text(jy_label, "Joystick");
        // lv_obj_set_style_text_align(jy_label, LV_TEXT_ALIGN_CENTER, 0);
        // lv_obj_align(jy_label, LV_ALIGN_TOP_MID, 0, 180);

        // 圆形摇杆外框
        joystick_circle = lv_obj_create(lv_scr_act());
        lv_obj_set_size(joystick_circle, 100, 100);
        lv_obj_align(joystick_circle, LV_ALIGN_TOP_MID, 0, 190);
        lv_obj_set_style_bg_color(joystick_circle, lv_color_white(), 0);  // 白色背景
        lv_obj_set_style_border_color(joystick_circle, lv_color_black(), 0);  // 黑色边框
        lv_obj_set_style_border_width(joystick_circle, 2, 0);
        lv_obj_set_style_radius(joystick_circle, LV_RADIUS_CIRCLE, 0);  // 圆形
        lv_obj_set_style_pad_all(joystick_circle, 0, 0);  // 去除内边距！
        lv_obj_clear_flag(joystick_circle, LV_OBJ_FLAG_SCROLLABLE);  // 禁用滚动条

        // 红色圆形指示点
        joystick_ball = lv_obj_create(joystick_circle);
        lv_obj_set_size(joystick_ball, 12, 12);  // 稍微大一点更明显
        lv_obj_set_pos(joystick_ball, 44, 44);  // 中心位置：(100-12)/2 = 44
        lv_obj_set_style_bg_color(joystick_ball, lv_color_make(0, 0, 255), 0);  // 红色
        lv_obj_set_style_border_width(joystick_ball, 0, 0);
        lv_obj_set_style_radius(joystick_ball, LV_RADIUS_CIRCLE, 0);  // 圆形
        lv_obj_set_style_pad_all(joystick_ball, 0, 0);  // 确保小球也没有内边距

        lv_obj_t *btn_label = lv_label_create(lv_scr_act());
        lv_label_set_text(btn_label, "Press Button to Toggle LED!");
        lv_obj_set_style_text_align(btn_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(btn_label, LV_ALIGN_TOP_MID, 0, 380);  // 放在LED上方
    }
}

void lv_example_btn_1(void)
{
    lv_obj_t *label;

    // Hardware Demo按钮
    lv_obj_t *hw_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(hw_btn, hw_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(hw_btn, LV_ALIGN_TOP_MID, 0, 40);

    label = lv_label_create(hw_btn);
    lv_label_set_text(label, "Hardware Demo");
    lv_obj_center(label);

    // Calculator按钮
    lv_obj_t *calc_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(calc_btn, calculator_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(calc_btn, LV_ALIGN_TOP_MID, 0, 90);

    label = lv_label_create(calc_btn);
    lv_label_set_text(label, "Calculator");
    lv_obj_center(label);
}

void task0(void *pvParam)
{
    lv_obj_clean(lv_scr_act());
    vTaskDelay(100 / portTICK_PERIOD_MS);

    img1 = lv_img_create(lv_scr_act());
    LV_IMG_DECLARE(star);
    lv_img_set_src(img1, &star);
    lv_obj_align(img1, LV_ALIGN_DEFAULT, 0, 0);
    lv_example_btn_1();

    for (;;)
    {
        if (adc_en)
        {
            adc_init();
            // Make sure GPIO is high-impedance, no pullups etc
            adc_gpio_init(26);
            adc_gpio_init(27);

            for (;;)
            {
                char buf[50];

                adc_select_input(0);
                uint adc_x_raw = adc_read();
                adc_select_input(1);
                uint adc_y_raw = adc_read();

                // 简化的映射逻辑 - 映射到0-88范围
                const uint adc_max = (1 << 12) - 1;  // 4095
                const int max_pos = 88;  // 100-12=88 (外框100，小球12)
                
                // 直接线性映射
                int ball_x = (adc_x_raw * max_pos) / adc_max;
                int ball_y = max_pos - (adc_y_raw * max_pos) / adc_max;  // Y轴反向

                // 移动小球位置
                lv_obj_set_pos(joystick_ball, ball_x, ball_y);

                vTaskDelay(200 / portTICK_PERIOD_MS);
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void task1(void *pvParam)
{
    for (;;)
    {
        lv_task_handler();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

int main()
{
    stdio_init_all();

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    UBaseType_t task0_CoreAffinityMask = (1 << 0);
    UBaseType_t task1_CoreAffinityMask = (1 << 1);

    TaskHandle_t task0_Handle = NULL;

    xTaskCreate(task0, "task0", 2048, NULL, 1, &task0_Handle);
    vTaskCoreAffinitySet(task0_Handle, task0_CoreAffinityMask);

    TaskHandle_t task1_Handle = NULL;
    xTaskCreate(task1, "task1", 2048, NULL, 2, &task1_Handle);
    vTaskCoreAffinitySet(task1_Handle, task1_CoreAffinityMask);

    vTaskStartScheduler();

    return 0;
}