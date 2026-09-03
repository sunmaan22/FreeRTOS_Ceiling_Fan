/*
 * disp.c  -  FND 멀티플렉싱 + Text LCD (독립 포트)
 *
 *   FND : 세그먼트 = PORTC (공통애노드 → ~패턴), 자리 = PD4..PD7 (ON=HIGH)
 *   LCD : 데이터 = PORTA, RS = PG0, RW = PG1, E = PG2  (RW 항상 0 = 쓰기)
 *         부저(PG3) 와 PORTG 공유하지만 전부 단일비트 sbi/cbi → 원자적, 뮤텍스 불필요
 *   클럭 16MHz 기준 딜레이.
 */
#include "board.h"

#include "FreeRTOS.h"
#include "task.h"

#include <util/delay.h>
#include <string.h>

#include "disp.h"

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

#define FND_DIG_MSK ((uint8_t)(0x0F << FND_DIG_SHIFT))

static inline void fnd_all_off(void)
{
#if FND_DIG_ACTIVE_LOW
    FND_DIG_PORT |= FND_DIG_MSK;
#else
    FND_DIG_PORT &= (uint8_t)~FND_DIG_MSK;
#endif
}

void vFndTask(void *pvParameters)
{
    uint8_t d = 0;
    TickType_t next;

    (void)pvParameters;

    FND_SEG_DDR  = 0xFF;
    FND_DIG_DDR |= FND_DIG_MSK;
#if FND_SEG_ON_LOW
    FND_SEG_PORT = 0xFF;
#else
    FND_SEG_PORT = 0x00;
#endif
    fnd_all_off();

    next = xTaskGetTickCount();
    for (;;)
    {
        uint8_t seg[4], sd;

        taskENTER_CRITICAL();
        seg[0] = s_seg[0]; seg[1] = s_seg[1];
        seg[2] = s_seg[2]; seg[3] = s_seg[3];
        taskEXIT_CRITICAL();

        fnd_all_off();
        sd = seg[d];
#if FND_SEG_ON_LOW
        FND_SEG_PORT = (uint8_t)~sd;
#else
        FND_SEG_PORT = sd;
#endif
#if FND_DIG_ACTIVE_LOW
        FND_DIG_PORT &= (uint8_t)~(1u << (FND_DIG_SHIFT + d));
#else
        FND_DIG_PORT = (uint8_t)((FND_DIG_PORT & ~FND_DIG_MSK)
                                 | (uint8_t)(1u << (FND_DIG_SHIFT + d)));
#endif
        d = (uint8_t)((d + 1) & 3);
        vTaskDelayUntil(&next, pdMS_TO_TICKS(2));
    }
}

/* ===================== LCD ===================== */
static char             s_line[2][LCD_COLS];
static volatile uint8_t s_dirty = 0x03;      /* bit0=row0, bit1=row1 */

void lcd_set_line(uint8_t row, const char *text)
{
    char t[LCD_COLS];
    uint8_t i = 0, diff = 0;

    while (i < LCD_COLS && text[i]) { t[i] = text[i]; i++; }
    while (i < LCD_COLS)            { t[i] = ' ';     i++; }

    taskENTER_CRITICAL();
    for (i = 0; i < LCD_COLS; i++)
        if (s_line[row][i] != t[i]) { s_line[row][i] = t[i]; diff = 1; }
    if (diff) s_dirty |= (uint8_t)(1u << row);
    taskEXIT_CRITICAL();
}

void disp_init(void)
{
    memset((void *)s_line, ' ', sizeof(s_line));
    s_dirty = 0x03;
}

/* --- 저수준 LCD (RW=0 고정, write 전용). vLcdTask 문맥에서만 --- */
static void lcd_bus(uint8_t rs, uint8_t value)
{
    if (rs) LCD_CTRL_PORT |=  (uint8_t)(1u << LCD_RS_BIT);
    else    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_RS_BIT);
    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_RW_BIT);       /* RW = 0 */

    LCD_DATA_PORT = value;
    _delay_us(5);
    LCD_CTRL_PORT |=  (uint8_t)(1u << LCD_E_BIT);        /* E = 1 */
    _delay_us(50);
    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_E_BIT);        /* E = 0 -> 래치 */
    _delay_us(500);
}
static void lcd_cmd(uint8_t c) { lcd_bus(0, c); }

static void lcd_hw_init(void)
{
    LCD_DATA_DDR  = 0xFF;
    LCD_CTRL_DDR |= (uint8_t)((1u << LCD_RS_BIT) | (1u << LCD_RW_BIT) | (1u << LCD_E_BIT));
    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_RS_BIT);
    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_RW_BIT);
    LCD_CTRL_PORT &= (uint8_t)~(1u << LCD_E_BIT);

    _delay_ms(120);
    lcd_cmd(0x38); _delay_ms(10);
    lcd_cmd(0x38); _delay_us(300);
    lcd_cmd(0x38); _delay_us(300);
    lcd_cmd(0x38);                  /* 8bit, 2line, 5x8 */
    lcd_cmd(0x0C);                  /* display on, cursor off */
    lcd_cmd(0x01); _delay_ms(5);    /* clear */
    lcd_cmd(0x06);                  /* entry mode */
}

void vLcdTask(void *pvParameters)
{
    char l[2][LCD_COLS];
    uint8_t r, c, dirty;

    (void)pvParameters;

    lcd_hw_init();

    for (;;)
    {
        taskENTER_CRITICAL();
        dirty = s_dirty;
        if (dirty)
        {
            memcpy(l, (const void *)s_line, sizeof(l));
            s_dirty = 0;
        }
        taskEXIT_CRITICAL();

        for (r = 0; r < 2; r++)
        {
            if (!(dirty & (1u << r))) continue;
            lcd_cmd((uint8_t)(0x80 | (r ? 0x40 : 0x00)));
            for (c = 0; c < LCD_COLS; c++)
                lcd_bus(1, (uint8_t)l[r][c]);
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}
