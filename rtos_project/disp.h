/*
 * disp.h  -  FND(7세그 4자리) + Text LCD 드라이버
 *   FND 와 LCD 는 서로 다른 포트 → 독립 태스크.
 */
#ifndef DISP_H
#define DISP_H

#include <stdint.h>

#define FND_BLANK   0x00
#define FND_DASH    0x40
#define FND_DP      0x80

uint8_t fnd_font(uint8_t digit);
void    fnd_set(const uint8_t seg[4]);          /* seg[0] = 맨 왼쪽 */
void    lcd_set_line(uint8_t row, const char *text);   /* 16칸으로 잘림/패딩 */
void    disp_init(void);                        /* main() 에서 스케줄러 시작 전 */

void    vFndTask(void *pvParameters);
void    vLcdTask(void *pvParameters);

#endif /* DISP_H */
