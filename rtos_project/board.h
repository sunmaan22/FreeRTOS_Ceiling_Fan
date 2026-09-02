/*
 * board.h  -  선풍기 프로젝트 핀 배치
 *
 *   PORTC       = FND 세그먼트 (PC0=a … PC7=dp)  ＝  LCD 데이터 D0~D7   [공용]
 *   PB0 / PB1   = LCD RS / EN         (RW 는 GND 고정)
 *   PD4~PD7     = FND 자리선택 (PD4 = 맨 왼쪽 … PD7 = 맨 오른쪽, ON = HIGH)
 *   PE3         = 모터 PWM (OC3A / Timer3)
 *   PE4~PE7     = SW2~SW5 (active-low, 내부 풀업)
 *   PG3         = 부저 (active-high)
 *
 *   ※ LED 바는 PB0/PB1 이 LCD 제어선과 겹쳐서 제거함.
 *   ★ 클럭 8MHz 가정. 실제와 다르면 F_CPU / FreeRTOSConfig.h configCPU_CLOCK_HZ 수정.
 */
#ifndef BOARD_H
#define BOARD_H

#ifndef F_CPU
#define F_CPU 8000000UL
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

/* ---------- FND 세그먼트 ＝ LCD 데이터 (PORTC 공용) ---------- */
#define FND_SEG_PORT           PORTC
#define FND_SEG_DDR            DDRC
#define FND_SEG_ON_LOW         1        /* 세그먼트 ON = LOW (공통애노드) */

/* ---------- FND 자리선택 ---------- */
#define FND_DIG_PORT           PORTD
#define FND_DIG_DDR            DDRD
#define FND_DIG_SHIFT          4        /* PD4(맨왼쪽)~PD7(맨오른쪽) */
#define FND_DIG_ACTIVE_LOW     0        /* 자리 ON = HIGH */

/* ---------- Text LCD (HD44780, 8bit, RW=GND) ---------- */
#define LCD_DATA_PORT          PORTC    /* = FND 세그먼트와 공용 */
#define LCD_CTRL_PORT          PORTB
#define LCD_CTRL_DDR           DDRB
#define LCD_RS_BIT             0        /* PB0 */
#define LCD_EN_BIT             1        /* PB1 */
#define LCD_COLS               16

#endif /* BOARD_H */
