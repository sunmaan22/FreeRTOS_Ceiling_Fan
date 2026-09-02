#define F_CPU 8000000U
#include <avr/io.h>
#include <util/delay.h>
#define LCD_RS  0
#define LCD_EN  1
void lcd_out(unsigned char d){
	PORTA = d;                 /* ← 안 되면 여기를 PORTA / PORTF 로 바꿔 시도 */
	PORTB |= (1<<LCD_EN);  _delay_us(1);
	PORTB &= ~(1<<LCD_EN); _delay_us(1000);
}
void lcd_cmd(unsigned char c){ PORTB &= ~(1<<LCD_RS); lcd_out(c); }
void lcd_dat(unsigned char c){ PORTB |=  (1<<LCD_RS); lcd_out(c); }
void lcd_str(char*s){ while(*s) lcd_dat(*s++); }
int main(void){
	DDRC = 0xFF; DDRB = 0x03;
	_delay_ms(50);
	lcd_cmd(0x38); lcd_cmd(0x0C); lcd_cmd(0x01); _delay_ms(2); lcd_cmd(0x06);
	lcd_cmd(0x80); lcd_str("Hello World");
	lcd_cmd(0xC0); lcd_str("Thank you!");
	while(1);
}
