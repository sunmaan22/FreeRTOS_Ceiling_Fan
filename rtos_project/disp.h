/*
 * disp.h  -  FND(7세그 4자리) + Text LCD 통합 디스플레이
 *
 *   PORTC 를 FND 세그먼트와 LCD 데이터가 공유하므로 vDispTask 하나가 독점한다.
 */
#ifndef DISP_H
#define DISP_H

#include <stdint.h>

#define FND_BLANK   0x00
#define FND_DASH    0x40      /* g 세그먼트만 */
#define FND_DP      0x80      /* 도트 */

uint8_t fnd_font(uint8_t digit);

/* 4자리 세그먼트 패턴 (seg[0] = 맨 왼쪽). 논리값(비트1=점등), 반전은 드라이버가 처리 */
void fnd_set(const uint8_t seg[4]);

/* LCD 한 줄(row 0/1). 16칸으로 자르고 공백 패딩. 실제 출력은 vDispTask 가 조금씩 반영 */
void lcd_set_line(uint8_t row, const char *text);

/* main() 에서 스케줄러 시작 전 1회 */
void disp_init(void);

/* FND + LCD 통합 태스크 */
void vDispTask(void *pvParameters);

#endif /* DISP_H */
