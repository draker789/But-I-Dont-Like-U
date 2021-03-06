/*****************************************************************************
 *   EE2024 Assignment 2 CARE 2016/2017 Sem1
 *
 *   Copyright(C) 2016, Liu Ren Jie, Ong Ming Lun
 *   All rights reserved.
 *
/******************************************************************************/

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Libraries Import
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h"

#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "oled.h"
#include "rgb.h"
#include "temp.h"
#include "led7seg.h"
#include "rotary.h"
#include "light.h"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Define Global Constants
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define WARNING_LOWER			50
#define WARNING_UPPER 			1000
#define INDICATOR_TIME_UNIT		208
#define SSD_TIME_UNIT			1000
#define RGB_BLINK_TIME			333
#define JOYSTICK_TIME_UNIT		20
#define FULL_TIME_UNIT			2000
#define TEMP_SCALAR_DIV10 		1
#define NUM_HALF_PERIODS 		300

#define X1		10
#define X2		25
#define X3		40
#define X4		60

#define Y1		15
#define Y2		30
#define	Y3		50
#define Y4		60

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Declare Global Sensors Variables
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
uint32_t light = 0;
static float temperature;
static int8_t xoff = 0, yoff = 0, zoff = 0;
static int8_t x = 0, y = 0, z = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Declare Global text array
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static uint8_t text[100];					//for text msg with variables
static char* UART_msg = NULL;				//for text msg without variables

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Declare Global counters
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static int harvested = 0;
static int UART_msg_counter = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Declare Global Flags
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static bool Algae_Flag = false;
static bool Waste_Flag = false;

static bool Start_Flag = false;
static bool Date_Flag = false;
static bool Passive_Flag = false;
static bool Charge_Flag = false;

static bool FULL = false;
static bool EXIT = false;

static bool SW4 = false;
static bool next_SW4 = false;
static bool SW3 = false;

static bool uartUp = false;
static bool uartDown = false;
static bool uartRight = false;
static bool uartLeft = false;
static bool uartGet = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Declare Temperature Sensor related interrupt Global variables
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
uint32_t old_temp_ticks = 0;
uint32_t temp_time_period = 0;
int temp_period_count = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Set up msTicks related variables and functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Set up usTicks related variables and functions
// Generation of 100us for reading temperature sensor using GPIO Interrupt
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

volatile uint32_t usTicks = 0;

uint32_t getusTicks(void)
{
	return usTicks;
}

//Initialise Timer0 to produces a interrupt in every 100us
void init_timer(void){
	TIM_TIMERCFG_Type timer_cfg;
	TIM_MATCHCFG_Type match_cfg;

	//Timer0_PCLK frequency is retrieved and predetermined to be 100MHz by printing the function CLKPWR_GetPCLKSEL (uint32_t ClkType);
	timer_cfg.PrescaleOption = TIM_PRESCALE_TICKVAL;		//Prescale in absolute value
	timer_cfg.PrescaleValue = 25;							//Clear Prescale counter when PC == 25 and produce a tick in TC
	TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timer_cfg);			//Calls even more functions to set frequency of Timer0 = (Timer0_Peripheral_Clk_Freq / 4) = (100MHz / 4) = 25MHz

	match_cfg.ExtMatchOutputType = 0;						//External Match Register (EMR): Do nothing for external output pin if match
	match_cfg.IntOnMatch = ENABLE;							//Interrupt on Match Enabled
	match_cfg.MatchChannel = 0;								//Match to Timer0
	match_cfg.MatchValue = 100;								//Generate an interrupt when TC = 100 (ie. match MR0)
	match_cfg.ResetOnMatch = ENABLE;
	match_cfg.StopOnMatch = DISABLE;
	TIM_ConfigMatch(LPC_TIM0, &match_cfg);

	TIM_Cmd(LPC_TIM0, ENABLE);
	TIM_ResetCounter(LPC_TIM0);

}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Interrupt Handlers
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void EINT3_IRQHandler(void){
	if ((LPC_GPIOINT->IO2IntStatF>>10)& 0x1){		// Determine whether SW3 is pressed n falling edge
		if(Date_Flag){								//Trigger only in DATE Mode
			SW3 = true;
			// Clear GPIO Interrupt P2.10
			LPC_GPIOINT->IO2IntClr = 1<<10;
		}
		else{
			// Clear GPIO Interrupt P2.10
			LPC_GPIOINT->IO2IntClr = 1<<10;
		}
	}

	//Obtain Temperature
	if ((LPC_GPIOINT->IO0IntStatR>>2)& 0x1){						// Determine whether P0.2 (Temperature sensor GPIO) is at rising edge
		//Continue to add period counter if periods sampled < 151
		if(temp_period_count < 151){								// (temp_period_count < ((NUM_HALF_PERIODS / 2) + 1);
			temp_period_count++;
		}
		else{
			//Get time interval for 151 periods
			temp_time_period = getusTicks() - old_temp_ticks;
			old_temp_ticks = getusTicks();
			temp_period_count = 0;								//reset period counter

			//calculate temperature using formula
			temperature = ((((2*100*temp_time_period) / (NUM_HALF_PERIODS*TEMP_SCALAR_DIV10)) - 2731) / 10.0);
		}
		// Clear GPIO Interrupt P0.2
		LPC_GPIOINT->IO0IntClr = 1<<2;
	}
}

//Count instances of 100us using interrupt handlers and usTicks
void TIMER0_IRQHandler(void)
{
		usTicks++;
		LPC_TIM0->IR|=0x01;			//Clear Timer0 Interrupt by writing '1' to Interrupt Register
}

// When user keys in a character, UART receives it
// [W,A,S,D] = [UP,LEFT,DOWN,RIGHT]
// [SPACEBAR] = EXIT
void UART3_IRQHandler(void) {
	uint8_t data;
	//Receive one letter
	UART_Receive(LPC_UART3, &data, 1, BLOCKING);

	// Decode letter to up, down, left, right & EXIT
	if(data == 'w'){
		uartUp = true;
	}
	if(data == 's'){
		uartDown = true;
	}
	if(data == 'd'){
		uartRight = true;
	}
	if(data == 'a')	{
		uartLeft = true;
	}
	//if SPACEBAR key is pressed, exit CHARGE Mode
	if(data == ' '){
		EXIT = true;
	}

	//Triggers draw line on OLED function
	uartGet = true;
	//Clear Pending to complete RX request
	NVIC_ClearPendingIRQ(UART3_IRQn);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Abstracted Functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//Turn off LED Array from LED19 to LED4
static void Decrease_LED_array(uint8_t steps){
	uint16_t ledOn = 0;

	ledOn = 0xffff >> steps;

	pca9532_setLeds(ledOn, 0xffff);
}

//Turn on LED Array from LED4 to LED19
static void Increase_LED_array(uint8_t harvested){
	uint16_t ledOn = 0;

	ledOn = 0xffff << harvested;

	pca9532_setLeds((~ledOn), 0xffff);
}

void Sensors_Read(){

	light = light_read();
	acc_read(&x, &y, &z);
	x = x+xoff;
	y = y+yoff;
	z = z+zoff;
}

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
		//Blink none
		return;
	}
	else if(detected == 1){
		//Blink Blue
		GPIO_ClearValue(Blue_port,(Blue_state & (1 << Blue_pin)));
		GPIO_SetValue(Blue_port,((~Blue_state) & (1 << Blue_pin)));
	}
	else if(detected == 2){
		//Blink Red
		GPIO_ClearValue(Red_port,(Red_state & (1 << Red_pin)));
		GPIO_SetValue(Red_port,((~Red_state) & (1 << Red_pin)));
	}
	else if(detected == 3){
		//Check if Red and Blue LED are in opposite states
		if(((Red_state >> Red_pin) & 1) != ((Blue_state >> Blue_pin) & 1)){
			//Toggle only the Blue LED once to synchronize
			GPIO_ClearValue(Blue_port,(Blue_state & (1 << Blue_pin)));
			GPIO_SetValue(Blue_port,((~Blue_state) & (1 << Blue_pin)));
		}
		else{
			//Blink both Red and Blue LED
			GPIO_ClearValue(Blue_port,(Blue_state & (1 << Blue_pin)));
			GPIO_SetValue(Blue_port,((~Blue_state) & (1 << Blue_pin)));
			GPIO_ClearValue(Red_port,(Red_state & (1 << Red_pin)));
			GPIO_SetValue(Red_port,((~Red_state) & (1 << Red_pin)));
		}
	}
}

//Draws line on OLED using the Joystick or keyboard via UART
static void drawOled(uint8_t joyState, int arr[16])
{
    static int wait = 0;
    static uint8_t currX = 48;
    static uint8_t currY = 32;
    static uint8_t lastX = 0;
    static uint8_t lastY = 0;

    if ((joyState & JOYSTICK_CENTER) != 0) {
    	//Joystick pressed, Exiting CHARGE();
        EXIT = true;
        return;
    }

    if (wait++ < 3){
        return;
    }

    wait = 0;

    if (((joyState  & JOYSTICK_UP) != 0 && currY > 0) || ((uartUp) && currY > 0))  {
        currY--;
        uartUp = false;
    }

    if (((joyState & JOYSTICK_DOWN) != 0 && currY < OLED_DISPLAY_HEIGHT-1) || ((uartDown) && currY < OLED_DISPLAY_HEIGHT-1)) {
        currY++;
        uartDown = false;
    }

    if (((joyState & JOYSTICK_RIGHT) != 0 && currX < OLED_DISPLAY_WIDTH-1) || ((uartRight) && currX < OLED_DISPLAY_WIDTH-1)){
        currX++;
        uartRight = false;
    }

    if (((joyState & JOYSTICK_LEFT) != 0 && currX > 0) || ((uartLeft) && currX > 0)) {
        currX--;
        uartLeft = false;
    }

    if (lastX != currX || lastY != currY) {
        oled_putPixel(currX, currY, OLED_COLOR_WHITE);
        check_filled(currX, currY, arr);					//check for harvests
        Increase_LED_array(harvested);						//update LED array with number of harvests
        lastX = currX;
        lastY = currY;
    }
    uartGet = false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OLED-related Functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
}

void OLED_Update_EXIT(){
	int initial_time_full = getTicks();

	oled_clearScreen(OLED_COLOR_BLACK);
	while(!((check_time(FULL_TIME_UNIT, &initial_time_full)))){
		//Show msg on OLED for 2 seconds
		sprintf(text, "Fail to Charged");
		oled_putString(1, 20, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
		sprintf(text,"Returning to");
		oled_putString(1, 30, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
		sprintf(text,"PASSIVE MODE");
		oled_putString(1, 40, text, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	}
	oled_clearScreen(OLED_COLOR_BLACK);
}

//Place 16 pixels as biofuels on OLED
void place_biofuel(){
	oled_putPixel(X1, Y1,OLED_COLOR_WHITE);
	oled_putPixel(X1, Y2,OLED_COLOR_WHITE);
	oled_putPixel(X1, Y3,OLED_COLOR_WHITE);
	oled_putPixel(X1, Y4,OLED_COLOR_WHITE);
	oled_putPixel(X2, Y1,OLED_COLOR_WHITE);
	oled_putPixel(X2, Y2,OLED_COLOR_WHITE);
	oled_putPixel(X2, Y3,OLED_COLOR_WHITE);
	oled_putPixel(X2, Y4,OLED_COLOR_WHITE);
	oled_putPixel(X3, Y1,OLED_COLOR_WHITE);
	oled_putPixel(X3, Y2,OLED_COLOR_WHITE);
	oled_putPixel(X3, Y3,OLED_COLOR_WHITE);
	oled_putPixel(X3, Y4,OLED_COLOR_WHITE);
	oled_putPixel(X4, Y1,OLED_COLOR_WHITE);
	oled_putPixel(X4, Y2,OLED_COLOR_WHITE);
	oled_putPixel(X4, Y3,OLED_COLOR_WHITE);
	oled_putPixel(X4, Y4,OLED_COLOR_WHITE);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Flag-related Functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//Call function when SW3 EINT is triggered
void GET_INFORMATION(){
	Sensors_Read();
	OLED_Update();
	SW3 = false;
}

//Check if Algae is detected
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

//Check if Solid Waste is detected
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

//Check for condition from steady mode to PASSIVE mode (first time start up)
int MODE_TOGGLE_Start(){
	//SW4 is default pulled HIGH, check for LOW when pushed
	if (!((GPIO_ReadValue(1) >> 31) & 0x01)){
		Start_Flag = true;
		return 1;
	}
	else
		return 0;
}

//Check for condition to change to DATE Mode
void MODE_TOGGLE(int i){

	if (next_SW4 == true && i != 16) {
		SW4 = true;
		next_SW4 = false;
	}

	if (!((GPIO_ReadValue(1) >> 31) & 0x01)){		//Check if SW4 was pressed
		if (i != 16) {								//If SW4 is pressed when led7seg does not shows 'F'
			SW4 = true;
			return;
		}
		else {										//SW4 was pressed when led7seg shows 'F'
			next_SW4 = true;						//Update SW4 Flag in the next cycle
			return;
		}
	}
	else if (SW4 && (i == 16)){						//Check if SW4 was previously pressed and trigger when 7 Segment Display shows 'F'
		Date_Flag = true;
		SW4 = false;
		return;
	}
	else
		return;
}

//Check for condition to change to CHARGE Mode
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

//Check if biofuels fully harvested
void check_harvested(){
	if (harvested == 16){

		//Disable UART3 interrupt handler since it is not needed anymore
		NVIC_ClearPendingIRQ(UART3_IRQn);
		NVIC_DisableIRQ(UART3_IRQn);

		//Send msg to SAFE upon fully harvested
		UART_msg = "Biofuels fully harvested. Leaving CHARGE mode. \r\n";
		UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);

		OLED_Update_CHARGE();
		harvested = 0;
		pca9532_setLeds(0, 0xffff);					//turn off LED array
		FULL = true;
	}
	return;
}

//Check if exit is triggered while in CHARGE();
void check_exit(){
	if(EXIT){

		//Disable UART3 interrupt handler since it is not needed anymore
		NVIC_ClearPendingIRQ(UART3_IRQn);
		NVIC_DisableIRQ(UART3_IRQn);

		//Send msg to SAFE upon CHARGE mode exit trigger
		UART_msg = "Giving up on harvesting. Leaving CHARGE Mode. \r\n";
		UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);

		OLED_Update_EXIT();
		harvested = 0;
		pca9532_setLeds(0, 0xffff);					//turn off LED array
		FULL = true;

	}
	return;
}

//check for number of harvests
int check_filled(int currX, int currY, int arr[16]){
	if (currX == X1 && currY == Y1){
		if(arr[0] == 0){
			harvested++;
			arr[0] = 1;
		}
	}
	if (currX == X1 && currY == Y2){
		if(arr[1] == 0){
			harvested++;
			arr[1] = 1;
		}
	}
	if (currX == X1 && currY == Y3){
		if(arr[2] == 0){
			harvested++;
			arr[2] = 1;
		}
	}
	if (currX == X1 && currY == Y4){
		if(arr[3] == 0){
			harvested++;
			arr[3] = 1;
		};
	}
	if (currX == X2 && currY == Y1){
		if(arr[4] == 0){
			harvested++;
			arr[4] = 1;
		}
	}
	if (currX == X2 && currY == Y2){
		if(arr[5] == 0){
			harvested++;
			arr[5] = 1;
		}
	}
	if (currX == X2 && currY == Y3){
		if(arr[6] == 0){
			harvested++;
			arr[6] = 1;
		}
	}
	if (currX == X2 && currY == Y4){
		if(arr[7] == 0){
			harvested++;
			arr[7] = 1;
		}
	}
	if (currX == X3 && currY == Y1){
		if(arr[8] == 0){
			harvested++;
			arr[8] = 1;
		}
	}
	if (currX == X3 && currY == Y2){
		if(arr[9] == 0){
			harvested++;
			arr[9] = 1;
		}
	}
	if (currX == X3 && currY == Y3){
		if(arr[10] == 0){
			harvested++;
			arr[10] = 1;
		}
	}
	if (currX == X3 && currY == Y4){
		if(arr[11] == 0){
			harvested++;
			arr[11] = 1;
		}
	}
	if (currX == X4 && currY == Y1){
		if(arr[12] == 0){
			harvested++;
			arr[12] = 1;
		}
	}
	if (currX == X4 && currY == Y2){
		if(arr[13] == 0){
			harvested++;
			arr[13] = 1;
		}
	}
	if (currX == X4 && currY == Y3){
		if(arr[14] == 0){
			harvested++;
			arr[14] = 1;
		}
	}
	if (currX == X4 && currY == Y4){
		if(arr[15] == 0){
			harvested++;
			arr[15] = 1;
		}
	}
	else
		return 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UART related functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void send_status_SAFE(){
	if(Algae_Flag){
		//Send following msg to SAFE if Algae is dectected
		UART_msg = "Algae was Detected. \r\n";
		UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);
	}

	if(Waste_Flag){
		//Send following msg to SAFE if Waste was detected
		UART_msg = "Solid Wastes was Detected. \r\n";
		UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);
	}
	return;
}

void send_to_SAFE(){
	const char* Sensor_UART_one = "00%d_-_T%.1f_L%u_AX%d_AY%d_AZ%d\r\n";
	const char* Sensor_UART_ten = "0%d_-_T%.1f_L%u_AX%d_AY%d_AZ%d\r\n";
	const char* Sensor_UART_hundred = "%d_-_T%.1f_L%u_AX%d_AY%d_AZ%d\r\n";

	if (UART_msg_counter < 10){
		// send sensor values to SAFE, counter = 00x
		sprintf(text,Sensor_UART_one, UART_msg_counter, temperature, light, x, y, z);
		UART_Send(LPC_UART3, text, strlen(text), BLOCKING);
	}

	else if (UART_msg_counter > 99){
		// send sensor values to SAFE, counter = xxx
		sprintf(text,Sensor_UART_hundred, UART_msg_counter, temperature, light, x, y, z);
		UART_Send(LPC_UART3, text, strlen(text), BLOCKING);
	}

	else{
		// send sensor values to SAFE, counter = 0xx
		sprintf(text,Sensor_UART_ten, UART_msg_counter, temperature, light, x, y, z);
		UART_Send(LPC_UART3, text, strlen(text), BLOCKING);
	}

	UART_msg_counter++;						//Increment UART msg count

	return;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Mode Initialization Functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void priority_init(){
    //give Timer0 interrupt handler highest priority as it requires high timing precision
    uint32_t PG=5, PP=0b00, SP=0b011;
    uint32_t ans = NVIC_EncodePriority(PG,PP,SP);

    NVIC_SetPriority(TIMER0_IRQn, ans);
	NVIC_ClearPendingIRQ(TIMER0_IRQn);
	NVIC_EnableIRQ(TIMER0_IRQn);

    //Next highest priority given to EINT3 interrupt handler
	PG=5, PP=0b10, SP=0b011;
	ans = NVIC_EncodePriority(PG,PP,SP);
	NVIC_SetPriority(EINT3_IRQn,ans);
	NVIC_ClearPendingIRQ(EINT3_IRQn);
	NVIC_EnableIRQ(EINT3_IRQn);

	//Lowest priority given to UART3 interrupt handler
	PG=5, PP=0b11, SP=0b011;
	ans = NVIC_EncodePriority(PG,PP,SP);
	NVIC_SetPriority(UART3_IRQn,ans);
	NVIC_ClearPendingIRQ(UART3_IRQn);
	NVIC_EnableIRQ(UART3_IRQn);
	// Configure UART3 to enable RBR (Receiver Buffer Register) Interrupt
	UART_IntConfig(LPC_UART3, UART_INTCFG_RBR, ENABLE);
}

void passive_init(){
	Sensors_Read();
	OLED_Update_PASSIVE();
	OLED_Update();
	Date_Flag = false;
	Waste_Flag = false;
	Algae_Flag = false;
	SW4 = false;

	//Send msg to SAFE upon entering PASSIVE Mode
	UART_msg = "Entering PASSIVE Mode. \r\n";
	UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);

	return;
}

void charge_init(){
	//initialize CHARGE mode
	GPIO_ClearValue( 2, 1);			//turn off red led
	GPIO_ClearValue( 0, (1<<26) );	//turn off blue led
	led7seg_setChar('C', TRUE); 	//Show a 'C' on 7 segment display
	oled_clearScreen(OLED_COLOR_BLACK);
	place_biofuel();

	//Send msg to SAFE upon entering CHARGE Mode
	UART_msg = "Leaving PASSIVE Mode. Entering CHARGE Mode. \r\n";
	UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);

	return;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// 3 Main Modes loop
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void CHARGE(){
	uint8_t state = 0;
	int initial_time_Joystick = getTicks();
	int arr[16] ={0};

	FULL = false;
	EXIT = false;
	charge_init();

	while(!FULL){
		state = joystick_read();

		//Draw line if joystick is used
		if(check_time(JOYSTICK_TIME_UNIT, &initial_time_Joystick)){
			if ((state != 0)){
				drawOled(state, arr);
			}
		}
		//Draw line if keyboard is used
		if (uartGet){
			drawOled(0, arr);
		}

		//check if finish harvesting
		check_harvested();
		//check if exit is pressed
		check_exit();
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
	SW4 = false;

	//Disable UART3 interrupt handler since it is not used yet
	NVIC_ClearPendingIRQ(UART3_IRQn);
	NVIC_DisableIRQ(UART3_IRQn);

	while (!Date_Flag){
		passive_init();

		while(1){
			MODE_TOGGLE(i);										//checks if need to go to DATE mode
			MODE_TOGGLE_Charge(&count);							//checks if need to go to CHARGE mode

			//in CHARGE Mode
			while(Charge_Flag){
				//Enable UART3 interrupt handler to be used in CHARGE Mode
				NVIC_ClearPendingIRQ(UART3_IRQn);
				NVIC_EnableIRQ(UART3_IRQn);

				CHARGE();
				//Exited CHARGE Mode
				Charge_Flag = false;
				passive_init();
				i = 0;
				break;											//Go out of the loop and restart PASSIVE mode
			}

			if (check_time(SSD_TIME_UNIT, &initial_time_SSD)){	//Wait 1 second before updating 7 segment display

				if ((i == 5)||(i == 10)||(i == 15)){			//7 Segment Display showing '5', 'A', or 'F'
					Sensors_Read();
					OLED_Update();
					if(i == 15){								//7 Segment Display showing 'F'
						send_status_SAFE();						//Send msg to SAFE via UART
						send_to_SAFE();
					}
				}
				if(i == 16){									//restart 7 Segment Display from '0'
					i = 0;
				}
				if (Date_Flag){									//Go out of loop and exit PASSIVE mode, go to DATE();
					break;
				}
				led7seg_setChar(array[i], TRUE);				//Update 7 Segment Display
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

	//Send msg to SAFE upon entering DATE Mode
	UART_msg = "Leaving PASSIVE Mode. Entering DATE Mode. \r\n";
	UART_Send(LPC_UART3, (uint8_t *)UART_msg, strlen(UART_msg), BLOCKING);

	while(!Passive_Flag){
		steps = 0;
		led7seg_setChar(' ', FALSE); 	//turn of 7 segment display
		OLED_Update_DATE();

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
				GET_INFORMATION();												//Read sensors and update OLED
				send_to_SAFE();													//Send sensor values to SAFE via UART
			}
		}
	}
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Initialize protocols
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
	GPIO_SetDir(0, 1<<2, 0);				//Set as Input

	//SW4 MODE_TOGGLE
	//Use PIO1_4-WAKEUP -> P1.31
	PinCfg.Portnum = 1;
	PinCfg.Pinnum = 31;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(1, 1<<31, 0);				//Set as Input

	//SW3 GET_INFORMATION
	//Use PIO2_9 -> P2.10
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Funcnum = 1;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, 1<<10, 0);				//Set as Input

	//RED LED
	//Use PIO1_9 -> P2.0
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, 1<<0, 1);				//Set as Output

	//BLUE LED
	//Use PIO1_2 -> P0.26
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 26;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<26, 1);				//Set as Output

	//Rotary Switch I
	//Use PIO1_0 -> P0.24
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 24;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<24, 0);				//Set as Input
	//Rotary Switch II
	//Use PIO1_1 -> P0.25
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 25;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(0, 1<<25, 0);				//Set as Input
}

static void init_ssp(void)
{
	//Initialize 7 Segment Display and OLED
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

	// Initialize Light Sensor and Accelerometer and 16bit Port Expander (LED Array)
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

void pinsel_uart3 (void) {
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin (&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin (&PinCfg);
}

void init_uart (void) {
	UART_CFG_Type uartCfg;
	uartCfg.Baud_rate = 115200;
	uartCfg.Databits = UART_DATABIT_8;
	uartCfg.Parity = UART_PARITY_NONE;
	uartCfg.Stopbits = UART_STOPBIT_1;
	//pin select for uart3
	pinsel_uart3();
	//supply power & setup working par.s for uart3
	UART_Init(LPC_UART3, &uartCfg);
	//enable transmit for uart3
	UART_TxCmd(LPC_UART3, ENABLE);
}

//=============================================================================
// Main Function
//=============================================================================
int main (void) {

    init_i2c();
    init_ssp();
    init_GPIO();
    init_uart();

    pca9532_init();
    joystick_init();
    acc_init();
    oled_init();
    led7seg_init();
    light_init();
    light_enable();
    rotary_init();
    init_timer();
    SysTick_Config(SystemCoreClock/1000);
    priority_init();

	// Enable GPIO Interrupt P2.10 (Falling edge)
	LPC_GPIOINT->IO2IntEnF |= 1<<10;
	// Enable GPIO Interrupt P0.2  (Rising edge)
	LPC_GPIOINT->IO0IntEnR |= 1<<2;

	// Clear GPIO Interrupt P2.10
	LPC_GPIOINT->IO2IntClr = 1<<10;
	// Clear GPIO Interrupt P0.2
	LPC_GPIOINT->IO0IntClr = 1<<2;

    /*
	* Assume base board in zero-g position when reading first value.
	* Setup board for clean slate
	*/
	acc_read(&x, &y, &z);
	xoff = 0-x;
	yoff = 0-y;
	zoff = 0-z;

    oled_clearScreen(OLED_COLOR_BLACK);
	GPIO_ClearValue( 2, 1);			//turn off red led
	GPIO_ClearValue( 0, (1<<26) );	//turn off blue led

    while (1){

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

