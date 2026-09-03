/*
 * board.h  -  선풍기 프로젝트 핀 배치 (전부 독립 포트, 확정)
 *
 *   PORTA        = LCD 데이터 D0~D7
 *   PG0/PG1/PG2  = LCD RS / RW / E
 *   PG3          = 부저
 *   PORTB        = LED 바 (PB0~PB7, PB7 = 막대 바닥)
 *   PORTC        = FND 세그먼트 (PC0=a … PC7=dp),  ON = LOW (공통애노드)
 *   PD4~PD7      = FND 자리선택 (PD4 = 맨 왼쪽 … PD7 = 맨 오른쪽),  ON = HIGH
 *   PE3          = 모터 PWM (OC3A / Timer3)
 *   PE4~PE7      = SW2~SW5 (active-low, 내부 풀업)
 *
 *   클럭 = 16 MHz
 */
#ifndef BOARD_H
#define BOARD_H

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>

/* ---------- 모터 ---------- */
#define MOTOR_DDR              DDRE
#define MOTOR_BIT              PE3

/* ---------- 스위치 ---------- */
#define SW_PIN                 PINE
#define SW2_BIT                PE4
#define SW3_BIT                PE5
#define SW4_BIT                PE6
#define SW5_BIT                PE7

/* ---------- 부저 ---------- */
#define BUZZER_PORT            PORTG
#define BUZZER_DDR             DDRG
#define BUZZER_BIT             PG3
#define BUZZER_ENABLED         0        /* 조용한 곳 코딩용. 소리 켜려면 1 */

/* ---------- LED 바 ---------- */
#define LEDBAR_PORT            PORTB
#define LEDBAR_DDR             DDRB
#define LEDBAR_ACTIVE_HIGH     0        /* 이 보드: PBx=0 → LED on (active-low) */

/* ---------- FND ---------- */
#define FND_SEG_PORT           PORTC
#define FND_SEG_DDR            DDRC
#define FND_SEG_ON_LOW         1        /* 세그먼트 ON = LOW (공통애노드) */
#define FND_DIG_PORT           PORTD
#define FND_DIG_DDR            DDRD
#define FND_DIG_SHIFT          4        /* PD4(맨왼쪽)~PD7(맨오른쪽) */
#define FND_DIG_ACTIVE_LOW     0        /* 자리 ON = HIGH */

/* ---------- Text LCD (HD44780, 8bit) ---------- */
#define LCD_DATA_PORT          PORTA
#define LCD_DATA_DDR           DDRA
#define LCD_CTRL_PORT          PORTG
#define LCD_CTRL_DDR           DDRG
#define LCD_RS_BIT             0        /* PG0 */
#define LCD_RW_BIT             1        /* PG1 (쓰기전용, 항상 0) */
#define LCD_E_BIT              2        /* PG2 */
#define LCD_COLS               16

#endif /* BOARD_H */
