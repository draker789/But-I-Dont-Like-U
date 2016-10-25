/*****************************************************************************
 *   A demo example using several of the peripherals on the base board
 *
 *   Copyright(C) 2011, EE2024
 *   All rights reserved.
 *
 ******************************************************************************/
#include <stdbool.h>
#include <stdio.h>

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"


#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "oled.h"
#include "rgb.h"
#include "temp.h"
#include "led7seg.h"
#include "rotary.h"

#define WARNING_LOWER			50
#define WARNING_UPPER 			1000
#define INDICATOR_TIME_UNIT		208
#define SSD_TIME_UNIT			1000
#define RGB_BLINK_TIME			333
#define JOYSTICK_TIME_UNIT		30
#define FULL_TIME_UNIT			2000



/*****************************************************************************
 *   Mapping of Terms to Peripherals
 *
 *   MODE_TOGGLE			SW4
 *   GET_INFORMATION		SW3
 *
 ******************************************************************************/

/*
 * Set up msTicks related variables and functions
 * Call SysTick_Config(SystemCoreClock/1000);	in main later
 */
volatile uint32_t msTicks = 0;

void SysTick_Handler(void){
	msTicks++;
}

volatile uint32_t getTicks(void){
	return msTicks;
}

int check_time (int millis, int *initial_time) {
	int current_time = getTicks();

	//Check if millis have passed
	if((current_time - *initial_time) >= millis) {
		*initial_time = getTicks();
		return 1;
	}
	else {
		return 0;
	}
}

/*
 * Set up usTicks related variables and functions
 * For generation of 100us for temp_init(&getusTicks); in main later
 */

volatile uint32_t usTicks = 0;

uint32_t getusTicks(void)
{
	return usTicks;
}

//Produces a interrupt in Timer0 every 100us
void init_timer(void){
	TIM_TIMERCFG_Type timer_cfg;
	TIM_MATCHCFG_Type match_cfg;

	timer_cfg.PrescaleOption = TIM_PRESCALE_TICKVAL;
	timer_cfg.PrescaleValue = 25;							//Clear Prescale counter when PC == 25 and produce a tick in TC
	TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timer_cfg);

	match_cfg.ExtMatchOutputType = 0;
	match_cfg.IntOnMatch = ENABLE;
	match_cfg.MatchChannel = 0;
	match_cfg.MatchValue = 100;								//Generate an interrupt when TC match MR0;
	match_cfg.ResetOnMatch = ENABLE;
	match_cfg.StopOnMatch = DISABLE;
	TIM_ConfigMatch(LPC_TIM0, &match_cfg);

	TIM_Cmd(LPC_TIM0, ENABLE);
	TIM_ResetCounter(LPC_TIM0);

}

/*
 * Declare Global Sensors Variables
 */
uint32_t light = 0;
static float temperature;
static int8_t xoff = 0, yoff = 0, zoff = 0;
static int8_t x = 0, y = 0, z = 0;

/*
 * Declare Global text array
 */
static uint8_t text[100];

/*
 * Declare Global counter
 */
static int harvested = 0;

/*
 * Declare Global Flags
 */
static bool Algae_Flag = false;
static bool Waste_Flag = false;

static bool Start_Flag = false;
static bool Date_Flag = false;
static bool Passive_Flag = false;
static bool Charge_Flag = false;
static bool FULL = false;

static bool SW4 = false;
static bool SW3 = false;

/*
 * Interrupt Handlers
 */
void EINT3_IRQHandler(void){
// Determine whether SW3 is pressed n falling edge
	if ((LPC_GPIOINT->IO2IntStatF>>10)& 0x1){
		SW3 = true;
		// Clear GPIO Interrupt P2.10
		LPC_GPIOINT->IO2IntClr = 1<<10;
	}

}

//Count instances of 100us using interrupt handlers and usTicks
void TIMER0_IRQHandler(void)
{
		usTicks++;
		LPC_TIM0->IR|=0x01;
}

/*
 * Abstracted Functions
 */

static void Decrease_LED_array(uint8_t steps){
	uint16_t ledOn = 0;

	ledOn = 0xffff >> steps;

	pca9532_setLeds(ledOn, 0xffff);
}

static void Increase_LED_array(uint8_t harvested){
	uint16_t ledOn = 0;

	ledOn = 0xffff << harvested;

	pca9532_setLeds((~ledOn), 0xffff);
}

void Sensors_Read(){
	temperature = (temp_read()/10.0);
	light = light_read();
	acc_read(&x, &y, &z);
	x = x+xoff;
	y = y+yoff;
	z = z+zoff;

}

/*
 * OLED-related Functions
 */
void OLED_Update(){

	sprintf(text,"%.2f        ", temperature);
	oled_putString(37, 10, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"%d          ", light);
	oled_putString(37, 20, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"%d          ", x);
	oled_putString(37, 30, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"%d          ", y);
	oled_putString(37, 40, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"%d          ", z);
	oled_putString(37, 50, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
}

void OLED_Update_PASSIVE(){

	sprintf(text, "				PASSIVE		");
	oled_putString(1, 00, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"Temp:         ");
	oled_putString(1, 10, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"LUX :         ");
	oled_putString(1, 20, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"AX  :         ");
	oled_putString(1, 30, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"AY  :         ");
	oled_putString(1, 40, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"AZ  :         ");
	oled_putString(1, 50, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
}

void OLED_Update_DATE(){

	sprintf(text, "					DATE		");
	oled_putString(1, 00, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"Temp: DATE MODE        ");
	oled_putString(1, 10, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"LUX : DATE MODE        ");
	oled_putString(1, 20, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"AX  : DATE MODE        ");
	oled_putString(1, 30, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"AY  : DATE MODE        ");
	oled_putString(1, 40, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(text,"AZ  : DATE MODE        ");
	oled_putString(1, 50, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
}

void OLED_Update_CHARGE(){
	int initial_time_full = getTicks();

	oled_clearScreen(OLED_COLOR_BLACK);
	while(!((check_time(FULL_TIME_UNIT, &initial_time_full)))){
		//Show msg on OLED for 2 seconds
		sprintf(text, "Fully Charged");
		oled_putString(1, 20, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
		sprintf(text,"Returning to");
		oled_putString(1, 30, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
		sprintf(text,"PASSIVE MODE");
		oled_putString(1, 40, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	}
	oled_clearScreen(OLED_COLOR_BLACK);

	return;
}

//Place 16 pixels as biofuels on OLED
void place_biofuel(){
	oled_putPixel(10, 15,OLED_COLOR_WHITE);
	oled_putPixel(10, 30,OLED_COLOR_WHITE);
	oled_putPixel(10, 45,OLED_COLOR_WHITE);
	oled_putPixel(10, 60,OLED_COLOR_WHITE);
	oled_putPixel(20, 10,OLED_COLOR_WHITE);
	oled_putPixel(20, 25,OLED_COLOR_WHITE);
	oled_putPixel(20, 40,OLED_COLOR_WHITE);
	oled_putPixel(20, 55,OLED_COLOR_WHITE);
	oled_putPixel(40, 15,OLED_COLOR_WHITE);
	oled_putPixel(40, 30,OLED_COLOR_WHITE);
	oled_putPixel(40, 45,OLED_COLOR_WHITE);
	oled_putPixel(40, 60,OLED_COLOR_WHITE);
	oled_putPixel(50, 10,OLED_COLOR_WHITE);
	oled_putPixel(50, 25,OLED_COLOR_WHITE);
	oled_putPixel(50, 40,OLED_COLOR_WHITE);
	oled_putPixel(50, 55,OLED_COLOR_WHITE);

	return;
}

/*
 * Flag-related Functions
 */

void GET_INFORMATION(){			//Call function when SW3 EINT is triggered
	Sensors_Read();
	OLED_Update();
	SW3 = false;
}

int check_Algae(int light){
	if(!Algae_Flag){
		if ((light > WARNING_LOWER) && (light < WARNING_UPPER)){
			Algae_Flag = true;
			return 1;
		}
		else
			return 0;
	}
	else
		return 1;
}

int check_Waste(int light){
	if(!Waste_Flag){
		if ((light < WARNING_LOWER)){
			Waste_Flag = true;
			return 1;
		}
		else
			return 0;
	}
	else
		return 1;
}

//Check if Waste and Algae are detected, return corresponding scenario
int detection_case(int check_Waste, int check_Algae){
	if ((check_Waste) && (check_Algae)){
		return 3;
	}
	else if (check_Waste){
		return 2;
	}
	else if (check_Algae){
		return 1;
	}
	else
		return 0;
}

int MODE_TOGGLE_Start(){
	//SW4 is default pulled HIGH, check for LOW when pushed
	if (!((GPIO_ReadValue(1) >> 31) & 0x01)){
		Start_Flag = true;
		return 1;
	}
	else
		return 0;
}

int MODE_TOGGLE(int i){
	if (!((GPIO_ReadValue(1) >> 31) & 0x01)){		//Check if SW4 was pressed
		SW4 = true;
		return 2;
	}
	else if (SW4 && (i == 16)){						//Check if SW4 was previously pressed and trigger when 7 Segment Display shows 'F'
		Date_Flag = true;
		SW4 = false;
		return 1;
	}
	else
		return 0;
}

int MODE_TOGGLE_Charge(int *count){

	if((*count) >3){
		Charge_Flag = true;
		(*count = 0);
		return 2;
	}

	if (rotary_read() == 1 ){						//triggers when rotary switch is rotated
		(*count)++;
		return 1;
	}
	else
		return 0;
}

void check_harvested(){
	if (harvested == 16){
		OLED_Update_CHARGE();
		harvested = 0;
		pca9532_setLeds(0, 0xffff);					//turn off LED array
		FULL = true;
	}

	return;
}

//check for number of harvested
int check_filled(int currX, int currY, int arr[16]){
	if (currX == 10 && currY == 15){
		if(arr[0] == 0){
			harvested++;
			arr[0] = 1;
		}
	}
	if (currX == 10 && currY == 30){
		if(arr[1] == 0){
			harvested++;
			arr[1] = 1;
		}
	}
	if (currX == 10 && currY == 45){
		if(arr[2] == 0){
			harvested++;
			arr[2] = 1;
		}
	}
	if (currX == 10 && currY == 60){
		if(arr[3] == 0){
			harvested++;
			arr[3] = 1;
		};
	}
	if (currX == 20 && currY == 10){
		if(arr[4] == 0){
			harvested++;
			arr[4] = 1;
		}
	}
	if (currX == 20 && currY == 25){
		if(arr[5] == 0){
			harvested++;
			arr[5] = 1;
		}
	}
	if (currX == 20 && currY == 40){
		if(arr[6] == 0){
			harvested++;
			arr[6] = 1;
		}
	}
	if (currX == 20 && currY == 55){
		if(arr[7] == 0){
			harvested++;
			arr[7] = 1;
		}
	}
	if (currX == 40 && currY == 15){
		if(arr[8] == 0){
			harvested++;
			arr[8] = 1;
		}
	}
	if (currX == 40 && currY == 30){
		if(arr[9] == 0){
			harvested++;
			arr[9] = 1;
		}
	}
	if (currX == 40 && currY == 45){
		if(arr[10] == 0){
			harvested++;
			arr[10] = 1;
		}
	}
	if (currX == 40 && currY == 60){
		if(arr[11] == 0){
			harvested++;
			arr[11] = 1;
		}
	}
	if (currX == 50 && currY == 10){
		if(arr[12] == 0){
			harvested++;
			arr[12] = 1;
		}
	}
	if (currX == 50 && currY == 25){
		if(arr[13] == 0){
			harvested++;
			arr[13] = 1;
		}
	}
	if (currX == 50 && currY == 40){
		if(arr[14] == 0){
			harvested++;
			arr[14] = 1;
		}
	}
	if (currX == 50 && currY == 55){
		if(arr[15] == 0){
			harvested++;
			arr[15] = 1;
		}
	}
}

/*
 * MISC large Functions
 */

//Blink correct combination of LED according to the detected scenario
void blink_LED_PASSIVE(int detected){
	int Red_port = 2;
	int Red_pin = 0;
	int Red_state;

	int Blue_port = 0;
	int Blue_pin = 26;
	int Blue_state;

	Red_state = GPIO_ReadValue(Red_port);
	Blue_state = GPIO_ReadValue(Blue_port);

	if(detected == 0){
		return;
	}
	else if(detected == 1){
		GPIO_ClearValue(Blue_port,(Blue_state & (1 << Blue_pin)));
		GPIO_SetValue(Blue_port,((~Blue_state) & (1 << Blue_pin)));
	}
	else if(detected == 2){
		GPIO_ClearValue(Red_port,(Red_state & (1 << Red_pin)));
		GPIO_SetValue(Red_port,((~Red_state) & (1 << Red_pin)));
	}
	else if(detected == 3){
		if(((Red_state >> Red_pin) & 1) != ((Blue_state >> Blue_pin) & 1)){
			GPIO_ClearValue(Blue_port,(Blue_state & (1 << Blue_pin)));
			GPIO_SetValue(Blue_port,((~Blue_state) & (1 << Blue_pin)));
		}
		else{
			GPIO_ClearValue(Blue_port,(Blue_state & (1 << Blue_pin)));
			GPIO_SetValue(Blue_port,((~Blue_state) & (1 << Blue_pin)));
			GPIO_ClearValue(Red_port,(Red_state & (1 << Red_pin)));
			GPIO_SetValue(Red_port,((~Red_state) & (1 << Red_pin)));
		}
	}
}

//Draws line on OLED using the Joystick
static void drawOled(uint8_t joyState, int arr[16])
{
    static int wait = 0;
    static uint8_t currX = 48;
    static uint8_t currY = 32;
    static uint8_t lastX = 0;
    static uint8_t lastY = 0;

    if ((joyState & JOYSTICK_CENTER) != 0) {
        oled_clearScreen(OLED_COLOR_BLACK);
        return;
    }

    if (wait++ < 3)
        return;

    wait = 0;

    if ((joyState & JOYSTICK_UP) != 0 && currY > 0) {
        currY--;
    }

    if ((joyState & JOYSTICK_DOWN) != 0 && currY < OLED_DISPLAY_HEIGHT-1) {
        currY++;
    }

    if ((joyState & JOYSTICK_RIGHT) != 0 && currX < OLED_DISPLAY_WIDTH-1) {
        currX++;
    }

    if ((joyState & JOYSTICK_LEFT) != 0 && currX > 0) {
        currX--;
    }

    if (lastX != currX || lastY != currY) {
        oled_putPixel(currX, currY, OLED_COLOR_WHITE);
        check_filled(currX, currY, arr);					//check for harvests
        Increase_LED_array(harvested);						//update LED array with number of harvests
        lastX = currX;
        lastY = currY;
    }
}


/*
 * Initialization Functions
 */


void passive_reinit(){
	//Start up OLED after CHARGE mode
	Sensors_Read();
	OLED_Update_PASSIVE();
	OLED_Update();
	Date_Flag = false;
	Waste_Flag = false;
	Algae_Flag = false;

	return;
}

void charge_init(){
	//initialize CHARGE mode
	GPIO_ClearValue( 2, 1);			//turn off red led
	GPIO_ClearValue( 0, (1<<26) );	//turn off blue led
	led7seg_setChar('C', FALSE); 	//Show a 'C' on 7 segment display
	oled_clearScreen(OLED_COLOR_BLACK);
	place_biofuel();

	return;
}

/*
 * 3 Main Modes loop
 */
void CHARGE(){
	uint8_t state = 0;
	int initial_time_Joystick = getTicks();
	int arr[16] ={0};

	FULL = false;
	charge_init();

	while(!FULL){

		state = joystick_read();

		//Draw line
		if(check_time(JOYSTICK_TIME_UNIT, &initial_time_Joystick)){
			if (state != 0){
				drawOled(state, arr);
			}
		}
		//check if finish harvesting
		check_harvested();
	}
}

void PASSIVE(){
	int i = 0;
	int detected = 0;
	int initial_time_SSD = getTicks();
	int initial_time_RGB = getTicks();
	int count = 0;

	char array[16] = {'0','1','2','3','4','5','6','7','8','9','A','8','C','0','E','F'};

	Date_Flag = false;
	Waste_Flag = false;
	Algae_Flag = false;
	// Disable EINT3 interrupt, not used in PASSIVE mode
	NVIC_DisableIRQ(EINT3_IRQn);

	while (!Date_Flag){

		//Start up OLED in PASSIVE mode
		Sensors_Read();
		OLED_Update_PASSIVE();
		OLED_Update();

		while(1){
			MODE_TOGGLE(i);										//checks if need to go to DATE mode
			MODE_TOGGLE_Charge(&count);							//checks if need to go to CHARGE mode

			//in CHARGE mode
			while(Charge_Flag){
				CHARGE();
				Charge_Flag = false;
				passive_reinit();
				i = 0;
				break;											//Go out of the loop and restart PASSIVE mode
			}

			if (check_time(SSD_TIME_UNIT, &initial_time_SSD)){	//Wait 1 second before updating 7 segment display
				if (Date_Flag){									//Go out of loop and exit PASSIVE mode, go to DATE();
					break;
				}
				if ((i == 6)||(i == 11)||(i == 16)){			//7 Segment Display showing '5', 'A', or 'F'
					Sensors_Read();
					OLED_Update();
				}
				if(i == 16){
					i = 0;
				}
				led7seg_setChar(array[i], FALSE);
				i++;
			}

			if (check_time(RGB_BLINK_TIME, &initial_time_RGB)){						//Wait 333ms before blinking any RGB LED
				detected = detection_case(check_Waste(light), check_Algae(light));
				blink_LED_PASSIVE(detected);
			}
		}
	}
}

void DATE(){
	int steps = 0;
	int initial_time_LED = getTicks();

	Passive_Flag = false;
	GPIO_ClearValue( 2, 1);			//turn off red led
	GPIO_ClearValue( 0, (1<<26) );	//turn off blue led

	while(!Passive_Flag){
		steps = 0;
		led7seg_setChar(' ', FALSE); 	//turn of 7 segment display

		OLED_Update_DATE();

		// Clear GPIO Interrupt P2.10
		LPC_GPIOINT->IO2IntClr = 1<<10;
	    // Enable EINT3 interrupt
	    NVIC_EnableIRQ(EINT3_IRQn);

		while(1){
			Decrease_LED_array(steps);
			if(check_time(INDICATOR_TIME_UNIT, &initial_time_LED)){				//Wait for 208ms to turn off need LED in the LED array
				steps++;
				if(steps == 17){												//All LED off in LED Array
					Passive_Flag = true;
					break;														//Break out of loop and exit DATE mode, go to PASSIVE();
				}
			}
			if(SW3){															//Checks if SW3 EINT is triggered
				GET_INFORMATION();
			}
		}
	}
}



/*
 * Initialize protocols
 */

static void init_GPIO(void)
{
	PINSEL_CFG_Type PinCfg;

	//Temperature Sensor
	//Use J25 for PIO1_5 -> P0.2
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 2;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<2, 0);

	//SW4 MODE_TOGGLE
	//Use PIO1_4-WAKEUP -> P1.31
	PinCfg.Portnum = 1;
	PinCfg.Pinnum = 31;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(1, 1<<31, 0);

	//SW3 GET_INFORMATION
	//Use PIO2_9 -> P2.10
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Funcnum = 1;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, 1<<10, 0);

	//RED LED
	//Use PIO1_9 -> P2.0
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, 1<<0, 1);

	//BLUE LED
	//Use PIO1_2 -> P0.26
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 26;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<26, 1);

	//Rotary Switch
	//Use PIO1_0 -> P0.24
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 24;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<24, 0);

	//Use PIO1_1 -> P0.25
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 25;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<25, 0);
}

static void init_ssp(void)
{
	//Initialise 7 Segment Display
	//MOSI PIO0_9  -> P0.9
	//MISO PIO0_8  -> P0.8
	//SCK  PIO2_11 -> P0.7
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);
	SSP_ConfigStruct.ClockRate=20000000;
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void)
{
	PINSEL_CFG_Type PinCfg;

	// Initialize OLED
	//SDA: PIO0_5 -> P0.10
	//SCL: PIO0_4 -> P0.11
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	I2C_Init(LPC_I2C2, 100000);
	I2C_Cmd(LPC_I2C2, ENABLE);
}


int main (void) {

    init_i2c();
    init_ssp();
    init_GPIO();

    pca9532_init();
    joystick_init();
    acc_init();
    oled_init();
    led7seg_init();
    //temp_init(&getTicks);
    light_init();
    light_enable();
    rotary_init();

	NVIC_SetPriority(TIMER0_IRQn, 0);
	NVIC_ClearPendingIRQ(TIMER0_IRQn);
	NVIC_EnableIRQ(TIMER0_IRQn);

	init_timer();
    temp_init(&getusTicks);

    SysTick_Config(SystemCoreClock/1000);


    /*
	* Assume base board in zero-g position when reading first value.
	*/
	acc_read(&x, &y, &z);
	xoff = 0-x;
	yoff = 0-y;
	zoff = 0-z;


	// Enable GPIO Interrupt P2.10
	LPC_GPIOINT->IO2IntEnF |= 1<<10;

    oled_clearScreen(OLED_COLOR_BLACK);
	GPIO_ClearValue( 2, 1);			//turn off red led
	GPIO_ClearValue( 0, (1<<26) );	//turn off blue led

    while (1)
    {
    led7seg_setChar(' ', FALSE);
    MODE_TOGGLE_Start();			//Checks when SW4 is first pressed to start program

    while(Start_Flag){
    	//Toggle between PASSIVE mode and DATE mode
    	PASSIVE();
    	DATE();
    	}
	}
}

void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}

