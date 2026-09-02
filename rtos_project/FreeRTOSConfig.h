/*
 * FreeRTOSConfig.h  -  ATmega128 (Atmel Studio 7 / avr-gcc) 포팅용
 *
 * FreeRTOS Kernel V11.1.0 에 맞춰 작성됨.
 * (블로그의 구버전 config 는 그대로 쓰면 컴파일 안 됨 - 아래가 V11 대응 버전)
 *
 * 참고: https://blackinkgj.github.io/atmega128-freertos/
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <avr/io.h>

/*-----------------------------------------------------------
 * 스케줄러 동작
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_TIME_SLICING                   1

/*-----------------------------------------------------------
 * 하드웨어 / 타이밍
 *
 *  ★★★ configCPU_CLOCK_HZ 를 보드의 실제 크리스털 주파수로 반드시 맞추세요 ★★★
 *      - 블로그 예제 보드 : 7372800  (7.3728 MHz)
 *      - 흔한 국내 학습보드 : 16000000 (16 MHz)  또는 14745600 (14.7456 MHz)
 *  값이 틀리면 빌드는 되지만 tick(=시간) 이 안 맞습니다.
 *----------------------------------------------------------*/
#define configCPU_CLOCK_HZ                       ( ( unsigned long ) 8000000 )  /* board.h F_CPU 와 동일하게! */
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )

/* AVR(8bit) 이므로 16bit tick 사용.
 * V11 은 configUSE_16_BIT_TICKS 를 configTICK_TYPE_WIDTH_IN_BITS 로
 * 자동 변환하므로 아래 한 줄이면 충분함 (공식 AVR 데모와 동일 방식).
 * 주의: configTICK_TYPE_WIDTH_IN_BITS 와 동시에 정의하면 컴파일 에러. */
#define configUSE_16_BIT_TICKS                   1

/*-----------------------------------------------------------
 * 메모리 / 태스크
 *
 * ATmega128 은 SRAM 이 4KB 뿐입니다. HEAP_SIZE 를 너무 키우면
 * 링크는 되어도 런타임에 스택과 겹쳐 죽습니다. 3000 이하 권장.
 *----------------------------------------------------------*/
#define configMAX_PRIORITIES                     ( 6 )
#define configMINIMAL_STACK_SIZE                 ( ( unsigned short ) 110 )
/* 태스크 5개(스택 합 ~940) + 큐 + 뮤텍스 ≈ 1.4KB 사용.
 * 스케줄러가 시작 안 되거나 리셋되면 이 값을 2400~2600 으로 낮출 것. */
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 2800 ) )
#define configMAX_TASK_NAME_LEN                  ( 8 )
#define configSTACK_DEPTH_TYPE                   uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE         size_t

#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1

/*-----------------------------------------------------------
 * 커널 기능 on/off
 *----------------------------------------------------------*/
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configUSE_QUEUE_SETS                     0
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TRACE_FACILITY                 0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configQUEUE_REGISTRY_SIZE                0
#define configUSE_APPLICATION_TASK_TAG          0
#define configCHECK_FOR_STACK_OVERFLOW           0
#define configRECORD_STACK_HIGH_ADDRESS         0

/*-----------------------------------------------------------
 * 소프트웨어 타이머
 *----------------------------------------------------------*/
#define configUSE_TIMERS                         0
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                 5
#define configTIMER_TASK_STACK_DEPTH           ( configMINIMAL_STACK_SIZE )

/*-----------------------------------------------------------
 * 코루틴 (사용 안 함)
 *----------------------------------------------------------*/
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES        ( 2 )

/*-----------------------------------------------------------
 * assert - 문제 생기면 여기서 무한루프. 디버깅 시 브레이크포인트 걸기 좋음.
 *----------------------------------------------------------*/
#define configASSERT( x )   if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ; ; ); }

/*-----------------------------------------------------------
 * API 포함 여부
 *  - 여기서 정의하지 않은 INCLUDE_* 는 FreeRTOS.h 의 기본값(대부분 0)을 따름.
 *  - stream_buffer.c 는 INCLUDE_xTaskGetCurrentTaskHandle==1 을 강제하므로 켜둠.
 *----------------------------------------------------------*/
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_vTaskPrioritySet                 0
#define INCLUDE_uxTaskPriorityGet                0
#define INCLUDE_vTaskDelete                      0
#define INCLUDE_vTaskSuspend                     0

#endif /* FREERTOS_CONFIG_H */
