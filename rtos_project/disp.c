/*
 * disp.c  -  FND 멀티플렉싱 + Text LCD (PORTC 공유)
 *
 *   FND : 세그먼트 = PORTC (공통애노드 → ~패턴 출력), 자리 = PD4..PD7 (ON=HIGH)
 *   LCD : 데이터 = PORTC (공용!), RS = PB0, EN = PB1, RW = GND
 *
 *   PORTC 를 둘이 공유하므로 vDispTask 하나가 소유한다.
 *   LCD 한 글자를 쓸 때는 FND 자리(PD4~7)를 잠깐 꺼서 PORTC 의 LCD 데이터가
 *   FND 에 안 보이게 한다. LCD 갱신은 한 번에 한 글자씩(2ms마다) → 깜빡임 최소.
 */
#include "board.h"

#include "FreeRTOS.h"
#include "task.h"

#include <util/delay.h>
#include <string.h>

#include "disp.h"

#define LCD_CELLS   (LCD_COLS * 2)
#define FND_DIG_MSK ((uint8_t)(0x0F << FND_DIG_SHIFT))

/* ===================== FND ===================== */
static const uint8_t FONT[10] =
    { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

uint8_t fnd_font(uint8_t digit) { return FONT[digit % 10]; }

static volatile uint8_t s_seg[4] = { 0, 0, 0, 0 };

void fnd_set(const uint8_t seg[4])
{
    taskENTER_CRITICAL();
    s_seg[0] = seg[0]; s_seg[1] = seg[1];
    s_seg[2] = seg[2]; s_seg[3] = seg[3];
    taskEXIT_CRITICAL();
}

static inline void fnd_blank(void)
{
#if FND_DIG_ACTIVE_LOW
    FND_DIG_PORT |= FND_DIG_MSK;
#else
    FND_DIG_PORT &= (uint8_t)~FND_DIG_MSK;
#endif
}

/* ===================== LCD ===================== */
static char             s_line[LCD_CELLS];   /* 목표 */
static char             s_shown[LCD_CELLS];  /* 현재 LCD 표시 내용 */
static volatile uint8_t s_dirty = 1;
static uint8_t          s_scan  = 0;

void lcd_set_line(uint8_t row, const char *text)
{
    char t[LCD_COLS];
    uint8_t i = 0, diff = 0, base = (uint8_t)(row * LCD_COLS);

    while (i < LCD_COLS && text[i]) { t[i] = text[i]; i++; }
    while (i < LCD_COLS)            { t[i] = ' ';     i++; }

    taskENTER_CRITICAL();
    for (i = 0; i < LCD_COLS; i++)
        if (s_line[base + i] != t[i]) { s_line[base + i] = t[i]; diff = 1; }
    if (diff) s_dirty = 1;
    taskEXIT_CRITICAL();
}

void disp_init(void)
{
    memset(s_line,  ' ', LCD_CELLS);
    memset(s_shown, 0,   LCD_CELLS);   /* 전부 다르게 → 첫 동기화에서 전체 기록 */
    s_dirty = 1;
}

/* --- 저수준 LCD (RW=GND, write 전용). vDispTask 문맥에서만 --- */
static void lcd_bus(uint8_t rs, uint8_t value)
{
    if (rs) LCD_CTRL_PORT |=  (uint8_t)(1u << LCD_RS_BIT);
    else    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_RS_BIT);

    LCD_DATA_PORT = value;
    _delay_us(2);
    LCD_CTRL_PORT |=  (uint8_t)(1u << LCD_EN_BIT);
    _delay_us(4);
    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_EN_BIT);
    _delay_us(120);
}
static void lcd_cmd(uint8_t c) { lcd_bus(0, c); }

static void lcd_hw_init(void)
{
    LCD_CTRL_PORT &= (uint8_t)~((1u << LCD_RS_BIT) | (1u << LCD_EN_BIT));
    vTaskDelay(pdMS_TO_TICKS(45));           /* Vcc 안정 후 >40ms */

    lcd_cmd(0x30); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_cmd(0x30); _delay_us(200);
    lcd_cmd(0x30); _delay_us(200);
    lcd_cmd(0x38);                           /* 8bit, 2line, 5x8 */
    lcd_cmd(0x08);                           /* display off */
    lcd_cmd(0x01);                           /* clear */
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_cmd(0x06);                           /* entry mode */
    lcd_cmd(0x0C);                           /* display on, cursor off */
}

/* 바뀐 셀 1개를 LCD 에 기록. FND 는 잠깐 꺼둠. 기록했으면 1 반환 */
static uint8_t lcd_service_one(void)
{
    char tgt[LCD_CELLS];
    uint8_t k;

    taskENTER_CRITICAL();
    memcpy(tgt, s_line, LCD_CELLS);
    taskEXIT_CRITICAL();

    for (k = 0; k < LCD_CELLS; k++)
    {
        uint8_t idx = s_scan;
        s_scan = (uint8_t)((s_scan + 1u) % LCD_CELLS);

        if (tgt[idx] != s_shown[idx])
        {
            uint8_t r = (uint8_t)(idx / LCD_COLS);
            uint8_t c = (uint8_t)(idx % LCD_COLS);
            fnd_blank();                          /* PORTC 를 LCD 가 쓰는 동안 자리 off */
            lcd_cmd((uint8_t)(0x80 | (r ? 0x40 : 0x00) | c));
            lcd_bus(1, (uint8_t)tgt[idx]);
            s_shown[idx] = tgt[idx];
            return 1;
        }
    }
    return 0;   /* 더 바뀐 것 없음 */
}

/* ===================== 태스크 ===================== */
void vDispTask(void *pvParameters)
{
    uint8_t d = 0;
    TickType_t next;

    (void)pvParameters;

    FND_SEG_DDR   = 0xFF;                    /* PORTC 출력 (세그 + LCD 데이터) */
    FND_DIG_DDR  |= FND_DIG_MSK;             /* PD4~PD7 출력 */
    LCD_CTRL_DDR |= (uint8_t)((1u << LCD_RS_BIT) | (1u << LCD_EN_BIT));
    fnd_blank();
#if FND_SEG_ON_LOW
    FND_SEG_PORT = 0xFF;
#else
    FND_SEG_PORT = 0x00;
#endif

    lcd_hw_init();

    next = xTaskGetTickCount();
    for (;;)
    {
        uint8_t seg[4], sd;

        taskENTER_CRITICAL();
        seg[0] = s_seg[0]; seg[1] = s_seg[1];
        seg[2] = s_seg[2]; seg[3] = s_seg[3];
        taskEXIT_CRITICAL();

        /* --- LCD: 이번 패스에 한 글자 --- */
        if (s_dirty)
        {
            if (!lcd_service_one())
                s_dirty = 0;                 /* 완전히 동기화됨 */
        }

        /* --- FND: 한 자리 --- */
        fnd_blank();
        sd = seg[d];
#if FND_SEG_ON_LOW
        FND_SEG_PORT = (uint8_t)~sd;
#else
        FND_SEG_PORT = sd;
#endif
#if FND_DIG_ACTIVE_LOW
        FND_DIG_PORT &= (uint8_t)~(1u << (FND_DIG_SHIFT + d));
#else
        FND_DIG_PORT = (uint8_t)((FND_DIG_PORT & ~FND_DIG_MSK) | (uint8_t)(1u << (FND_DIG_SHIFT + d)));
#endif
        d = (uint8_t)((d + 1) & 3);

        vTaskDelayUntil(&next, pdMS_TO_TICKS(2));
    }
}
