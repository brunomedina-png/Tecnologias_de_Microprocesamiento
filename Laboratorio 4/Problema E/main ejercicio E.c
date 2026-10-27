#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include <math.h>
#include <stdbool.h>


// Pines
#define LED_PIN     PD6
#define BTN_HOLD    PD4
#define BTN_CAL     PD5
#define BUZZER_PIN  PB3


#define MPU_ADDR    0x68


// EEPROM
#define EE_MAGIC_ADDR  (uint16_t*)0x00
#define EE_OFF_X_ADDR  (uint16_t*)0x02
#define EE_OFF_Y_ADDR  (uint16_t*)0x04
#define EE_MAGIC_VAL   0x55AA


// Matriz de LEDs [Rojo, Verde, Azul]
uint8_t leds[64][3];


// TWI / I2C
void twi_init(void) {
	TWSR = 0x00;
	TWBR = 72;
	TWCR = (1 << TWEN);
}


void twi_start(void) {
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}


void twi_stop(void) {
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
	_delay_us(40);
}


void twi_write(uint8_t data) {
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}


uint8_t twi_read_ack(void) {
	TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
	while (!(TWCR & (1 << TWINT)));
	return TWDR;
}


uint8_t twi_read_nack(void) {
	TWCR = (1 << TWINT) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
	return TWDR;
}


// MPU6050
void mpu6050_init(void) {
	twi_start();
	twi_write((MPU_ADDR << 1) | 0);
	twi_write(0x6B);
	twi_write(0x00);
	twi_stop();
}


void mpu6050_read_accel(int16_t *ax, int16_t *ay, int16_t *az) {
	twi_start();
	twi_write((MPU_ADDR << 1) | 0);
	twi_write(0x3B);
	
	twi_start();
	twi_write((MPU_ADDR << 1) | 1);
	
	uint8_t xh = twi_read_ack();
	uint8_t xl = twi_read_ack();
	uint8_t yh = twi_read_ack();
	uint8_t yl = twi_read_ack();
	uint8_t zh = twi_read_ack();
	uint8_t zl = twi_read_nack();
	twi_stop();


	*ax = (int16_t)((xh << 8) | xl);
	*ay = (int16_t)((yh << 8) | yl);
	*az = (int16_t)((zh << 8) | zl);
}


// WS2812B
void sendBit(uint8_t bitVal) {
	if (bitVal) {
		PORTD |= (1 << LED_PIN);
		asm volatile ("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t");
		PORTD &= ~(1 << LED_PIN);
		asm volatile ("nop\n\tnop\n\tnop\n\tnop\n\t");
		} else {
		PORTD |= (1 << LED_PIN);
		asm volatile ("nop\n\tnop\n\tnop\n\t");
		PORTD &= ~(1 << LED_PIN);
		asm volatile ("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t");
	}
}


void sendByte(uint8_t byte) {
	for (uint8_t i = 0; i < 8; i++) {
		sendBit(byte & (1 << (7 - i)));
	}
}


void ws2812_show(void) {
	cli();
	for (int i = 0; i < 64; i++) {
		sendByte(leds[i][1]); // Verde
		sendByte(leds[i][0]); // Rojo
		sendByte(leds[i][2]); // Azul
	}
	sei();
	_delay_us(60);
}


void ws2812_clear(void) {
	for (int i = 0; i < 64; i++) {
		leds[i][0] = 0;
		leds[i][1] = 0;
		leds[i][2] = 0;
	}
}


void set_burbuja(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b) {
	ws2812_clear();
	for (uint8_t dr = 0; dr < 2; dr++) {
		for (uint8_t dc = 0; dc < 2; dc++) {
			uint8_t r_pos = row + dr;
			uint8_t c_pos = col + dc;
			if (r_pos < 8 && c_pos < 8) {
				uint8_t idx = r_pos * 8 + c_pos;
				leds[idx][0] = r;
				leds[idx][1] = g;
				leds[idx][2] = b;
			}
		}
	}
	ws2812_show();
}


// Buzzer
void buzzer_beep(uint16_t ms) {
	PORTB |= (1 << BUZZER_PIN);
	for (uint16_t i = 0; i < ms; i++) {
		_delay_ms(1);
	}
	PORTB &= ~(1 << BUZZER_PIN);
}


// EEPROM
void cargar_calibracion(int16_t *off_x, int16_t *off_y) {
	uint16_t magic = eeprom_read_word(EE_MAGIC_ADDR);
	if (magic == EE_MAGIC_VAL) {
		*off_x = (int16_t)eeprom_read_word(EE_OFF_X_ADDR);
		*off_y = (int16_t)eeprom_read_word(EE_OFF_Y_ADDR);
		} else {
		*off_x = 0;
		*off_y = 0;
	}
}


void guardar_calibracion(int16_t off_x, int16_t off_y) {
	eeprom_write_word(EE_MAGIC_ADDR, EE_MAGIC_VAL);
	eeprom_write_word(EE_OFF_X_ADDR, (uint16_t)off_x);
	eeprom_write_word(EE_OFF_Y_ADDR, (uint16_t)off_y);
}


int main(void) {
	DDRD |= (1 << LED_PIN);
	DDRD &= ~((1 << BTN_HOLD) | (1 << BTN_CAL));
	PORTD |= (1 << BTN_HOLD) | (1 << BTN_CAL);


	DDRB |= (1 << BUZZER_PIN);
	PORTB &= ~(1 << BUZZER_PIN);


	twi_init();
	mpu6050_init();


	int16_t offset_x = 0, offset_y = 0;
	cargar_calibracion(&offset_x, &offset_y);


	bool hold_activo = false;
	bool estado_prev_hold = false;
	bool estado_prev_cal = false;
	bool en_borde_previo = false;


	int16_t ax = 0, ay = 0, az = 0;
	float roll = 0.0f, pitch = 0.0f;
	float ang_x = 0.0f, ang_y = 0.0f;


	uint8_t col = 3, row = 3;
	uint8_t red = 0, green = 0, blue = 50;


	while (1) {
		// Botones
		bool btn_hold_pressed = !(PIND & (1 << BTN_HOLD));
		bool btn_cal_pressed  = !(PIND & (1 << BTN_CAL));


		if (btn_hold_pressed && !estado_prev_hold) {
			_delay_ms(20);
			if (!(PIND & (1 << BTN_HOLD))) {
				hold_activo = !hold_activo;
			}
		}
		estado_prev_hold = btn_hold_pressed;


		if (btn_cal_pressed && !estado_prev_cal) {
			_delay_ms(20);
			if (!(PIND & (1 << BTN_CAL))) {
				offset_x = (int16_t)lroundf(roll);
				offset_y = (int16_t)lroundf(pitch);
				guardar_calibracion(offset_x, offset_y);
				buzzer_beep(150);
			}
		}
		estado_prev_cal = btn_cal_pressed;


		// Lectura y angulos
		if (!hold_activo) {
			mpu6050_read_accel(&ax, &ay, &az);


			roll  = atan2f((float)ay, (float)az) * 57.2958f;
			pitch = atan2f((float)(-ax), sqrtf((float)ay * (float)ay + (float)az * (float)az)) * 57.2958f;


			ang_x = -(roll - (float)offset_x);
			ang_y = pitch - (float)offset_y;


			if (fabsf(ang_x) < 3.0f && fabsf(ang_y) < 3.0f) {
				col = 3;
				row = 3;
				red = 0; green = 0; blue = 50;
				} else {
				int16_t c_tmp = (int16_t)lroundf((ang_x + 30.0f) * 6.0f / 60.0f);
				int16_t r_tmp = (int16_t)lroundf((ang_y + 30.0f) * 6.0f / 60.0f);


				if (c_tmp < 0) c_tmp = 0;
				if (c_tmp > 6) c_tmp = 6;
				if (r_tmp < 0) r_tmp = 0;
				if (r_tmp > 6) r_tmp = 6;


				col = (uint8_t)c_tmp;
				row = (uint8_t)r_tmp;


				float abs_x = fabsf(ang_x);
				float abs_y = fabsf(ang_y);


				if (abs_x > 2.0f * abs_y) {
					red = 50; green = 0; blue = 0;
					} else if (abs_y > 2.0f * abs_x) {
					red = 0; green = 50; blue = 0;
					} else {
					red = 50; green = 50; blue = 0;
				}
			}
		}


		// LEDs y buzzer
		bool en_borde = (col == 0 || col == 6 || row == 0 || row == 6);


		if (en_borde && !en_borde_previo && !hold_activo) {
			buzzer_beep(50);
		}
		en_borde_previo = en_borde;


		set_burbuja(row, col, red, green, blue);


		_delay_ms(50);
	}


	return 0;
}