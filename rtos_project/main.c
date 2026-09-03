/*
 * main.c  -  ATmega128 + FreeRTOS 선풍기 + 타이머
 *
 *  [기본 모드]  SW2 선풍기 OFF / SW3 속도- / SW4 속도+ / SW5 타이머설정
 *  [타이머 설정] SW2 +10분 / SW3 +1분 / SW4 리셋 / SW5 확정→기본모드(카운트다운)
 *  [알람]        카운트다운 0 → (부저 옵션) + 화면 깜빡 → 정지 → 기본모드
 *
 *  FND    : 무타이머=속도숫자 / 타이머중=남은 분 / 설정중=설정 분(깜빡)
 *  LED 바 : 속도 1~8 (PB7 = 바닥). 알람 때 전체 깜빡.
 *  LCD    : L0 = "Wind speed : N/8" ,  L1 = 타이머 상태
 */
#include <avr/io.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "board.h"
#include "disp.h"

/* ================= IPC ================= */
static QueueHandle_t xKeyQueue;
static TaskHandle_t  xBuzzerTask;

enum { KEY_SW2 = 0, KEY_SW3, KEY_SW4, KEY_SW5 };
enum { KEV_PRESS = 0, KEV_REPEAT };
typedef struct { uint8_t sw; uint8_t type; } KeyEvent_t;

/* ================= 앱 상태 ================= */
typedef enum { MODE_BASIC, MODE_TIMER_SET, MODE_ALARM } Mode_t;

static volatile Mode_t   s_mode  = MODE_BASIC;
static uint8_t  s_speed    = 0;           /* 0..8 */
static uint16_t s_totalMin = 0;           /* 타이머 설정값 (분, 0~9999) */
static volatile uint8_t  s_armed = 0;
static uint32_t s_remain  = 0;            /* 남은 초 */

/* ================= 하드웨어 헬퍼 ================= */
static void motor_pwm_init(void)
{
    MOTOR_DDR |= (1 << MOTOR_BIT);
    TCCR3A = (1 << COM3A1) | (1 << WGM30);      /* 비반전, Fast PWM 8bit */
    TCCR3B = (1 << WGM32)  | (1 << CS31);       /* 분주 /8 */
    OCR3A  = 0;
}
static void motor_set(uint8_t step)
{
    OCR3A = (uint8_t)((uint16_t)step * 255u / 8u);
}
static void buzzer(uint8_t on)
{
#if BUZZER_ENABLED
    if (on) BUZZER_PORT |=  (1 << BUZZER_BIT);
    else    BUZZER_PORT &= ~(1 << BUZZER_BIT);
#else
    (void)on;
#endif
}
static void ledbar(uint8_t n)              /* PB7 부터 아래로 n칸 */
{
    uint8_t mask;
    if (n == 0)      mask = 0x00;
    else if (n >= 8) mask = 0xFF;
    else             mask = (uint8_t)~((uint8_t)((1u << (8 - n)) - 1u));
#if LEDBAR_ACTIVE_HIGH
    LEDBAR_PORT = mask;
#else
    LEDBAR_PORT = (uint8_t)~mask;
#endif
}

/* ================= 숫자 → 문자열 (4자리 0채움) ================= */
static void num4str(char *b, uint16_t v)
{
    b[0] = (char)('0' + (v / 1000) % 10);
    b[1] = (char)('0' + (v / 100)  % 10);
    b[2] = (char)('0' + (v / 10)   % 10);
    b[3] = (char)('0' + v % 10);
}

/* ================= FND: 4자리 정수 ================= */
static void fnd_num4(uint16_t v, uint8_t blankMask)
{
    uint8_t s[4];
    s[0] = fnd_font((uint8_t)((v / 1000) % 10));
    s[1] = fnd_font((uint8_t)((v / 100)  % 10));
    s[2] = fnd_font((uint8_t)((v / 10)   % 10));
    s[3] = fnd_font((uint8_t)(v % 10));
    for (uint8_t i = 0; i < 4; i++)
        if (blankMask & (1 << i)) s[i] = FND_BLANK;
    fnd_set(s);
}

/* ================= 화면 갱신 ================= */
static void update_display(uint8_t blink)
{
    char l0[17], l1[17];
    uint8_t i;
    uint16_t remMin = (uint16_t)((s_remain + 59u) / 60u);

    for (i = 0; i < 16; i++) { l0[i] = ' '; l1[i] = ' '; }
    l0[16] = 0; l1[16] = 0;

    /* ---------- FND + LED ---------- */
    if (s_mode == MODE_BASIC && !s_armed)
    {
        uint8_t s[4] = { FND_BLANK, FND_BLANK, FND_BLANK, fnd_font(s_speed) };
        fnd_set(s);
        ledbar(s_speed);
    }
    else if (s_mode == MODE_BASIC && s_armed)
    {
        fnd_num4(remMin, 0);
        ledbar(s_speed);
    }
    else if (s_mode == MODE_TIMER_SET)
    {
        fnd_num4(s_totalMin, blink ? 0x0F : 0x00);
        ledbar(s_speed);
    }
    else /* MODE_ALARM */
    {
        uint8_t s[4];
        uint8_t v = blink ? FND_DASH : FND_BLANK;
        s[0] = s[1] = s[2] = s[3] = v;
        fnd_set(s);
        ledbar(blink ? 8 : 0);
    }

    /* ---------- LCD ---------- */
    {                                            /* L0 : "Wind speed : 3/8" (16) */
        const char *p = "Wind speed :  /8";
        for (i = 0; p[i]; i++) l0[i] = p[i];
        l0[13] = (char)('0' + s_speed);
    }
    if (s_mode == MODE_ALARM)
    {
        const char *p = "*** TIME  UP ***";
        for (i = 0; p[i]; i++) l1[i] = p[i];
    }
    else if (s_mode == MODE_TIMER_SET)
    {
        const char *p = "Set timer:0000 m";
        for (i = 0; p[i]; i++) l1[i] = p[i];
        num4str(&l1[10], s_totalMin);
    }
    else if (s_armed)
    {
        const char *p = "Time left:0000 m";
        for (i = 0; p[i]; i++) l1[i] = p[i];
        num4str(&l1[10], remMin);
    }
    else
    {
        const char *p = "Timer : OFF";
        for (i = 0; p[i]; i++) l1[i] = p[i];
    }

    lcd_set_line(0, l0);
    lcd_set_line(1, l1);
}

/* ================= 키 처리 ================= */
static void handle_key(const KeyEvent_t *e)
{
    if (s_mode == MODE_ALARM)
    {
        s_armed = 0;
        s_mode  = MODE_BASIC;
        return;
    }

    if (s_mode == MODE_BASIC)
    {
        switch (e->sw)
        {
        case KEY_SW2: s_speed = 0; break;
        case KEY_SW3: if (s_speed > 0) s_speed--; break;
        case KEY_SW4: if (s_speed < 8) s_speed++; break;
        case KEY_SW5:
            if (e->type == KEV_PRESS)
            {
                if (s_armed)
                    s_totalMin = (uint16_t)((s_remain + 59u) / 60u);
                s_mode = MODE_TIMER_SET;
            }
            return;
        }
        motor_set(s_speed);
    }
    else /* MODE_TIMER_SET : 총값 = 분. 오래 누르면 증가폭 가속 */
    {
        static uint8_t rep = 0, repKey = 0xFF;
        uint16_t big;

        if (e->type == KEV_PRESS)          rep = 0;
        else if (e->sw == repKey)          { if (rep < 40) rep++; }
        else                               rep = 0;
        repKey = e->sw;

        big = (rep >= 20) ? 100u : (rep >= 8) ? 10u : 1u;   /* 가속 배수 */

        switch (e->sw)
        {
        case KEY_SW2: s_totalMin = (uint16_t)(s_totalMin + 10u * big);
                      if (s_totalMin > 9999) s_totalMin = 0; break;
        case KEY_SW3: s_totalMin = (uint16_t)(s_totalMin + big);
                      if (s_totalMin > 9999) s_totalMin = 0; break;
        case KEY_SW4: s_totalMin = 0; rep = 0; break;
        case KEY_SW5:
            if (e->type == KEV_PRESS)
            {
                s_remain = (uint32_t)s_totalMin * 60u;
                s_armed  = (s_totalMin > 0) ? 1 : 0;
                s_mode   = MODE_BASIC;
            }
            return;
        }
    }
}

/* ================= 태스크 ================= */
static void vKeyTask(void *pv)
{
    const uint8_t bit[4] = { SW2_BIT, SW3_BIT, SW4_BIT, SW5_BIT };
    uint8_t  shift[4] = { 0, 0, 0, 0 };
    uint8_t  down[4]  = { 0, 0, 0, 0 };
    uint16_t held[4]  = { 0, 0, 0, 0 };
    TickType_t next;

    (void)pv;
    next = xTaskGetTickCount();

    for (;;)
    {
        for (uint8_t k = 0; k < 4; k++)
        {
            uint8_t raw = (uint8_t)((SW_PIN >> bit[k]) & 1);   /* 1 = 안눌림 */
            uint8_t pressed = (uint8_t)!raw;
            uint8_t s3, on, off;

            shift[k] = (uint8_t)((shift[k] << 1) | pressed);
            s3  = (uint8_t)(shift[k] & 0x07);
            on  = (uint8_t)(s3 == 0x07);
            off = (uint8_t)(s3 == 0x00);

            if (on && !down[k])
            {
                KeyEvent_t e = { k, KEV_PRESS };
                down[k] = 1; held[k] = 0;
                xQueueSend(xKeyQueue, &e, 0);
            }
            else if (off && down[k])
            {
                down[k] = 0;
            }
            else if (down[k] && on && k != KEY_SW5)
            {
                held[k]++;
                if (held[k] >= 50 && ((held[k] - 50) % 12) == 0)
                {
                    KeyEvent_t e = { k, KEV_REPEAT };
                    xQueueSend(xKeyQueue, &e, 0);
                }
            }
        }
        vTaskDelayUntil(&next, pdMS_TO_TICKS(10));
    }
}

static void vBuzzerTask(void *pv)
{
    (void)pv;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (uint8_t i = 0; i < 5; i++)
        {
            buzzer(1); vTaskDelay(pdMS_TO_TICKS(200));
            buzzer(0); vTaskDelay(pdMS_TO_TICKS(200));
        }
        taskENTER_CRITICAL();
        s_armed = 0;
        s_mode  = MODE_BASIC;
        taskEXIT_CRITICAL();
    }
}

static void vAppTask(void *pv)
{
    KeyEvent_t e;
    TickType_t prev;
    uint16_t   msAcc = 0;
    uint16_t   blinkCnt = 0;

    (void)pv;

    motor_pwm_init();
    motor_set(0);

    prev = xTaskGetTickCount();
    for (;;)
    {
        if (xQueueReceive(xKeyQueue, &e, pdMS_TO_TICKS(40)) == pdTRUE)
            handle_key(&e);

        {
            TickType_t now = xTaskGetTickCount();
            uint16_t   dt  = (uint16_t)(now - prev);
            prev = now;

            msAcc = (uint16_t)(msAcc + dt);
            while (msAcc >= configTICK_RATE_HZ)
            {
                msAcc = (uint16_t)(msAcc - configTICK_RATE_HZ);
                if (s_armed && s_mode == MODE_BASIC && s_remain > 0)
                {
                    s_remain--;
                    if (s_remain == 0)
                    {
                        s_mode  = MODE_ALARM;
                        s_speed = 0;
                        motor_set(0);
                        xTaskNotifyGive(xBuzzerTask);
                    }
                }
            }
        }

        blinkCnt++;
        update_display((uint8_t)((blinkCnt / 6) & 1));   /* 약 250ms 주기 */
    }
}

/* ================= main ================= */
int main(void)
{
    DDRE  &= ~((1 << SW2_BIT) | (1 << SW3_BIT) | (1 << SW4_BIT) | (1 << SW5_BIT));
    PORTE |=  (1 << SW2_BIT) | (1 << SW3_BIT) | (1 << SW4_BIT) | (1 << SW5_BIT);
    LEDBAR_DDR = 0xFF;
    ledbar(0);
    BUZZER_DDR |= (1 << BUZZER_BIT);
    buzzer(0);

    disp_init();
    xKeyQueue = xQueueCreate(8, sizeof(KeyEvent_t));

    xTaskCreate(vFndTask,    "FND", configMINIMAL_STACK_SIZE + 40,  NULL, 4, NULL);
    xTaskCreate(vKeyTask,    "KEY", configMINIMAL_STACK_SIZE + 50,  NULL, 3, NULL);
    xTaskCreate(vBuzzerTask, "BUZ", configMINIMAL_STACK_SIZE + 40,  NULL, 3, &xBuzzerTask);
    xTaskCreate(vAppTask,    "APP", configMINIMAL_STACK_SIZE + 160, NULL, 2, NULL);
    xTaskCreate(vLcdTask,    "LCD", configMINIMAL_STACK_SIZE + 90,  NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;) { }
    return 0;
}
