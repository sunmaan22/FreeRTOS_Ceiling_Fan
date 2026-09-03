/*
 * main.c  -  ATmega128 + FreeRTOS 실링팬 + 타이머 + 야간모드
 *
 *  [BASIC]      SW2 팬 OFF / SW3 속도- / SW4 속도+ / SW5 타이머설정
 *  [TIMER_SET]  SW2 +10분(가속) / SW3 +1분(가속) / SW4 리셋 / SW5 확정→BASIC
 *  [ALARM]      카운트다운 0 → 팬 정지 + LED바 깜빡 + "TIME UP" 3초 표시
 *                 (삼성 세탁기풍 멜로디, 부저는 board.h BUZZER_ENABLED 로 on/off) → BASIC
 *  [NIGHT]      CDS 가 N초 연속 어두움 확정 → "GOOD NIGHT" + 팬 끌지 확인
 *                 SW4(PE6) = 예(팬 OFF) / SW5(PE7) = 아니오 / 30초 무응답 = 아니오
 *                 확정 후 1시간 동안 CDS 재확인 안 함
 *
 *  FND : 풍속 숫자만 (0~8)
 *  LED : 풍속 1~8 막대 (PB7 = 바닥)
 *  LCD : L0 = "RTOS Ceiling Fan" (야간모드 시 "GOOD NIGHT")
 *        L1 = 타이머 상태 (초 단위)  /  야간모드 시 확인 문구
 */
#include <avr/io.h>
#include <stdint.h>
#include <util/delay_basic.h>

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
typedef enum { MODE_BASIC, MODE_TIMER_SET, MODE_ALARM, MODE_NIGHT } Mode_t;

static volatile Mode_t   s_mode  = MODE_BASIC;
static uint8_t  s_speed    = 0;           /* 0..8 */
static uint16_t s_totalMin = 0;           /* 타이머 설정값 (분, 0~9999) */
static volatile uint8_t  s_armed = 0;
static uint32_t s_remain  = 0;            /* 남은 초 */
static uint8_t  s_alarmSec = 0;           /* 알람 경과 초 (표시 분기용) */
static volatile uint8_t  s_nightReq = 0;  /* vCdsTask → vAppTask : 야간모드 요청 */

/* ================= 하드웨어 헬퍼 ================= */
static void motor_pwm_init(void)
{
    MOTOR_DDR |= (1 << MOTOR_BIT);
    TCCR3A = (1 << COM3A1) | (1 << WGM30);
    TCCR3B = (1 << WGM32)  | (1 << CS31);       /* Fast PWM 8bit, /8 */
    OCR3A  = 0;
}
static void motor_set(uint8_t step)
{
    OCR3A = (uint8_t)((uint16_t)step * 255u / 8u);
}
/* 한 음 재생. hz=0 은 쉼표. BUZZER_ENABLED 0 이면 소리 없이 시간만 소비.
 * 음량을 낮추려고 50% 사각파 대신 듀티 25% 펄스로 구동.
 * (부저 활성 시엔 음 길이 동안 busy-wait → vBuzzerTask 는 낮은 우선순위) */
static void tone(uint16_t hz, uint16_t ms)
{
#if BUZZER_ENABLED
    if (hz == 0) { vTaskDelay(pdMS_TO_TICKS(ms)); return; }
    {
        uint16_t period = (uint16_t)(1000000UL / hz);          /* 주기 us */
        uint32_t full   = (uint32_t)period * 4u;               /* @16MHz: _delay_loop_2 = 4cyc/iter */
        uint16_t hi     = (uint16_t)(full / 4u);               /* on  = 25%  (음량↓) */
        uint16_t lo     = (uint16_t)(full - hi);               /* off = 75% */
        uint16_t n      = (uint16_t)((uint32_t)hz * ms / 1000u);
        uint16_t i;
        for (i = 0; i < n; i++)
        {
            BUZZER_PORT |=  (1 << BUZZER_BIT); _delay_loop_2(hi);
            BUZZER_PORT &= ~(1 << BUZZER_BIT); _delay_loop_2(lo);
        }
        BUZZER_PORT &= ~(1 << BUZZER_BIT);
    }
#else
    (void)hz;
    vTaskDelay(pdMS_TO_TICKS(ms));
#endif
}

/* 삼성 세탁기 종료음 느낌 ("봄의 소리 왈츠" 도입부 상승 프레이즈 근사, 약 1.7초).
 * {주파수Hz, 길이ms}.  G4=392 C5=523 D5=587 E5=659 F5=698 G5=784 */
static const uint16_t ALARM_MELODY[][2] = {
    {392,140},{523,140},{659,140},{587,140},{523,140},{587,280},
    {659,140},{698,140},{784,420},
};
#define ALARM_MELODY_LEN  ((uint8_t)(sizeof(ALARM_MELODY) / sizeof(ALARM_MELODY[0])))
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

/* ================= 문자열 헬퍼 ================= */
static void put2(char *b, uint8_t v)
{
    b[0] = (char)('0' + (v / 10) % 10);
    b[1] = (char)('0' + v % 10);
}

/* "HH:MM:SS" (8글자) 를 b 에 기록 */
static void hms(char *b, uint32_t sec)
{
    uint16_t h = (uint16_t)(sec / 3600u);
    if (h > 99) h = 99;
    put2(&b[0], (uint8_t)h);
    b[2] = ':';
    put2(&b[3], (uint8_t)((sec / 60u) % 60u));
    b[5] = ':';
    put2(&b[6], (uint8_t)(sec % 60u));
}

/* ================= 화면 갱신 ================= */
static void update_display(uint8_t blink)
{
    char l0[17], l1[17];
    uint8_t i;

    for (i = 0; i < 16; i++) { l0[i] = ' '; l1[i] = ' '; }
    l0[16] = 0; l1[16] = 0;

    /* ---------- FND : 풍속 숫자만 ---------- */
    {
        uint8_t s[4] = { FND_BLANK, FND_BLANK, FND_BLANK, fnd_font(s_speed) };
        fnd_set(s);
    }

    /* ---------- LED 바 : 풍속 ---------- */
    if (s_mode == MODE_ALARM) ledbar(blink ? 8 : 0);
    else                      ledbar(s_speed);

    /* ---------- LCD ---------- */
    if (s_mode == MODE_NIGHT)
    {
        const char *a = "GOOD NIGHT";
        const char *b = "OFF? SW4=Y SW5=N";
        for (i = 0; a[i]; i++) l0[i] = a[i];
        for (i = 0; b[i]; i++) l1[i] = b[i];
    }
    else
    {
        const char *a = "RTOS Ceiling Fan";
        for (i = 0; a[i]; i++) l0[i] = a[i];

        if (s_mode == MODE_ALARM)
        {
            /* 첫 1초는 "00:00:00" 을 그대로 보여준 뒤 문구로 전환 */
            const char *p = (s_alarmSec <= 1) ? "Left 00:00:00" : "*** TIME  UP ***";
            for (i = 0; p[i]; i++) l1[i] = p[i];
        }
        else if (s_mode == MODE_TIMER_SET)
        {
            const char *p = "Set  00:00:00";           /* HH:MM:SS */
            for (i = 0; p[i]; i++) l1[i] = p[i];
            hms(&l1[5], (uint32_t)s_totalMin * 60u);
        }
        else if (s_armed)
        {
            const char *p = "Left 00:00:00";
            for (i = 0; p[i]; i++) l1[i] = p[i];
            hms(&l1[5], s_remain);
        }
        else
        {
            const char *p = "Timer : OFF";
            for (i = 0; p[i]; i++) l1[i] = p[i];
        }
    }

    lcd_set_line(0, l0);
    lcd_set_line(1, l1);
}

/* ================= 키 처리 ================= */
static void handle_key(const KeyEvent_t *e)
{
    if (s_mode == MODE_NIGHT)
    {
        if (e->type != KEV_PRESS) return;
        if (e->sw == KEY_SW4)          /* 예 → 팬 끔 */
        {
            s_speed = 0;
            motor_set(0);
            s_mode = MODE_BASIC;
        }
        else if (e->sw == KEY_SW5)     /* 아니오 → 그대로 */
        {
            s_mode = MODE_BASIC;
        }
        return;
    }

    if (s_mode == MODE_ALARM)
    {
        s_armed    = 0;
        s_totalMin = 0;
        s_mode     = MODE_BASIC;
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

        if (e->type == KEV_PRESS)   rep = 0;
        else if (e->sw == repKey)   { if (rep < 40) rep++; }
        else                        rep = 0;
        repKey = e->sw;

        big = (rep >= 20) ? 100u : (rep >= 8) ? 10u : 1u;

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

/* ================= ADC (CDS) ================= */
static uint16_t adc_read(uint8_t ch)
{
    ADMUX  = (uint8_t)((1 << REFS0) | (ch & 0x1F));   /* AVCC 기준 */
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

/* ================= 태스크 ================= */
static void vCdsTask(void *pv)
{
    uint16_t darkCnt = 0;
    uint32_t cooldown = 0;
    TickType_t next;

    (void)pv;

    DDRF  &= (uint8_t)~(1 << CDS_ADC_CH);             /* PF1 입력 */
    PORTF &= (uint8_t)~(1 << CDS_ADC_CH);             /* 풀업 off */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  /* enable, /128 */
    (void)adc_read(CDS_ADC_CH);                       /* 첫 변환 버림 */

    next = xTaskGetTickCount();
    for (;;)
    {
        uint16_t v = adc_read(CDS_ADC_CH);
#if CDS_DARK_INVERT
        uint8_t dark = (v > CDS_DARK_LEVEL);
#else
        uint8_t dark = (v < CDS_DARK_LEVEL);
#endif
        if (cooldown > 0)
        {
            cooldown--;                              /* 이 루프 = 1초 */
            darkCnt = 0;
        }
        else if (dark)
        {
            if (darkCnt < 60000) darkCnt++;
            if (darkCnt >= CDS_DARK_CONFIRM)
            {
                s_nightReq = 1;
                cooldown = CDS_RECHECK_SEC;          /* 1시간 재확인 금지 */
                darkCnt = 0;
            }
        }
        else
        {
            darkCnt = 0;
        }

        vTaskDelayUntil(&next, pdMS_TO_TICKS(1000));
    }
}

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
            uint8_t raw = (uint8_t)((SW_PIN >> bit[k]) & 1);
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
    uint8_t i;
    (void)pv;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* 알람 시작 알림 */
        for (i = 0; i < ALARM_MELODY_LEN; i++)
        {
            tone(ALARM_MELODY[i][0], ALARM_MELODY[i][1]);
            tone(0, 15);                           /* 음 사이 짧은 쉼 */
            taskYIELD();                           /* 다른 태스크 숨통 */
        }
        /* 모드/타이머 해제는 vAppTask 의 3초 알람 타이머가 담당 */
    }
}

static void vAppTask(void *pv)
{
    KeyEvent_t e;
    TickType_t prev;
    uint16_t   msAcc = 0;
    uint16_t   blinkCnt = 0;
    uint8_t    nightSec = 0;

    (void)pv;

    motor_pwm_init();
    motor_set(0);

    prev = xTaskGetTickCount();
    for (;;)
    {
        if (xQueueReceive(xKeyQueue, &e, pdMS_TO_TICKS(40)) == pdTRUE)
            handle_key(&e);

        /* CDS 야간모드 요청 (BASIC 일 때만 진입) */
        if (s_nightReq)
        {
            s_nightReq = 0;
            if (s_mode == MODE_BASIC) { s_mode = MODE_NIGHT; nightSec = 0; }
        }

        {
            TickType_t now = xTaskGetTickCount();
            uint16_t   dt  = (uint16_t)(now - prev);
            prev = now;

            msAcc = (uint16_t)(msAcc + dt);
            while (msAcc >= configTICK_RATE_HZ)
            {
                msAcc = (uint16_t)(msAcc - configTICK_RATE_HZ);

                /* 카운트다운 : s_remain 이 "진짜" 1→0 으로 내려갈 때만 알람.
                 * (armed && remain==0 만으로 발동하면 임의로 부저가 울림) */
                if (s_armed && s_remain > 0 &&
                    (s_mode == MODE_BASIC || s_mode == MODE_NIGHT))
                {
                    s_remain--;
                    if (s_remain == 0)
                    {
                        s_mode   = MODE_ALARM;
                        s_speed  = 0;
                        s_alarmSec = 0;
                        motor_set(0);
                        /* 부저는 알람 1초 뒤 (00:00:00 을 먼저 보여준 뒤) */
                    }
                }

                /* 알람 : 1초간 "00:00:00" → 이후 "TIME UP" + 부저 → 총 4초 뒤 Timer:OFF */
                if (s_mode == MODE_ALARM)
                {
                    s_alarmSec++;
                    if (s_alarmSec == 2) xTaskNotifyGive(xBuzzerTask);
                    if (s_alarmSec >= 5)
                    {
                        s_armed    = 0;
                        s_totalMin = 0;          /* 만료된 설정값 리셋 */
                        s_mode     = MODE_BASIC;
                    }
                }

                /* 야간모드 30초 무응답 → 아니오(그대로) */
                if (s_mode == MODE_NIGHT && ++nightSec >= 30)
                    s_mode = MODE_BASIC;
            }
        }

        blinkCnt++;
        update_display((uint8_t)((blinkCnt / 6) & 1));
    }
}

/* ================= main ================= */
int main(void)
{
    DDRE  &= ~((1 << SW2_BIT) | (1 << SW3_BIT) | (1 << SW4_BIT) | (1 << SW5_BIT));
    PORTE |=  (1 << SW2_BIT) | (1 << SW3_BIT) | (1 << SW4_BIT) | (1 << SW5_BIT);
    LEDBAR_DDR = 0xFF;
    ledbar(0);
    BUZZER_DDR  |=  (1 << BUZZER_BIT);
    BUZZER_PORT &= ~(1 << BUZZER_BIT);

    disp_init();
    xKeyQueue = xQueueCreate(8, sizeof(KeyEvent_t));

    xTaskCreate(vFndTask,    "FND", configMINIMAL_STACK_SIZE + 40,  NULL, 4, NULL);
    xTaskCreate(vKeyTask,    "KEY", configMINIMAL_STACK_SIZE + 50,  NULL, 3, NULL);
    xTaskCreate(vBuzzerTask, "BUZ", configMINIMAL_STACK_SIZE + 40,  NULL, 1, &xBuzzerTask);
    xTaskCreate(vAppTask,    "APP", configMINIMAL_STACK_SIZE + 160, NULL, 2, NULL);
    xTaskCreate(vLcdTask,    "LCD", configMINIMAL_STACK_SIZE + 90,  NULL, 1, NULL);
    xTaskCreate(vCdsTask,    "CDS", configMINIMAL_STACK_SIZE + 30,  NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;) { }
    return 0;
}
