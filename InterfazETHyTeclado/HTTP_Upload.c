/*------------------------------------------------------------------------------
 * MDK Middleware - Component ::Network  y Component ::USB:Host
 * Copyright (c) 2004-2018 ARM Germany GmbH. All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    HTTP_Upload.c
 * Purpose: Proyecto de IoT
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "RTE_Components.h" 

#include "rl_net.h"                     // Keil.MDK-Pro::Network:CORE
#include "rl_fs.h"                      // Keil.MDK-Pro::File System:CORE
#include "rl_usb.h"                     /* RL-USB function prototypes         */

#include "hx711.h"											//libreria del modulo hx711

#include "stm32f7xx_hal.h"              // Keil::Device:STM32Cube HAL:Common

#include "Board_LED.h"                  // ::Board Support:LED
#include "Board_GLCD.h"                 // ::Board Support:Graphic LCD
#include "GLCD_Config.h"                // Keil.MCBSTM32F400::Board Support:Graphic LCD

#ifdef RTE_Compiler_EventRecorder
#include "EventRecorder.h"
#endif

// Main stack size must be multiple of 8 Bytes
#define APP_MAIN_STK_SZ (1024U)
uint64_t app_main_stk[APP_MAIN_STK_SZ / 8];
const osThreadAttr_t app_main_attr = {
  .stack_mem  = &app_main_stk[0],
  .stack_size = sizeof(app_main_stk)
};

extern GLCD_FONT GLCD_Font_16x24; //tipo de fuente de letras
extern int stdout_init (void);
static HX711 balanza;
static int tara;


/* Thread IDs */
static osThreadId_t TID_Display;

/* IP address change notification */
void netDHCP_Notify (uint32_t if_num, uint8_t option, const uint8_t *val, uint32_t len) {

  (void)if_num;
  (void)val;
  (void)len;

  if (option == NET_DHCP_OPTION_IP_ADDRESS) {
    /* IP address change, trigger LCD update */
    osThreadFlagsSet (TID_Display, 0x01);
  }
}

/*-----------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------
*Thread 'HX711' : Captura datos y los presenta por pantalla 
 *----------------------------------------------------------------------------*/
static __NO_RETURN void CeldaDeCarga (void *arg) {
	char v[10]={0};
	GLCD_DrawString(1*100, 1*24, "PACIFICSOFT S.A ");
	
		
	char valor[10]={0};
	char b[25]={0};
	char borar[20]={0};
	float pesa ;
	
	while (1)
  {		
		//sprintf(borar,"offsetF=%i",balanza.offset);
		//GLCD_DrawString(120,5*24,borar);
		
		//sprintf(b,"HX711_Value=%i",HX711_Value(balanza));
		//GLCD_DrawString(120,6*24,b);
		
		
		//(2.5/15678.0);//2.5kg - 201596
		
    sprintf(valor,"Peso=%i",HX711_Value(balanza)-tara);
		GLCD_DrawString(1*150, 7*24, valor);
	  
		osDelay (1);
  }	
}

/*------------------------------------------------------------------------------
*Thread 'keyboard' : detecta tecla e imprime por pantalla 
 *----------------------------------------------------------------------------*/
 static __NO_RETURN void keyboard (void *arg) {
  usbStatus usb_status;                 // USB status
  usbStatus hid_status;                 // HID status
  int       status;
  int       ch;                         // Character
  uint8_t   con = 0U;                   // Connection status of keyboard

  (void)arg;

  status = stdout_init ();              // Initialize retargeted stdout
  if (status != 0) {
    for (;;) {}                         // Handle stdout init failure
  }

  usb_status = USBH_Initialize(0U);     // Initialize USB Host 0
  if (usb_status != usbOK) {
    for (;;) {}                         // Handle USB Host 0 init failure
  }
  usb_status = USBH_Initialize(1U);     // Initialize USB Host 1
  if (usb_status != usbOK) {
    for (;;) {}                         // Handle USB Host 1 init failure
  }

  for (;;) {
    hid_status = USBH_HID_GetDeviceStatus(0U);  // Get HID device status
    if (hid_status == usbOK) {
      if (con == 0U) {                  // If keyboard was not connected previously
        con = 1U;                       // Keyboard got connected
        printf("Teclado conectado!\n");
      }
    } else {
      if (con == 1U) {                  // If keyboard was connected previously
        con = 0U;                       // Keyboard got disconnected
        printf("\nTeclado Desconectado!\n");
      }
    }
    if (con != 0U) {                    // If keyboard is active
      ch = USBH_HID_GetKeyboardKey(0U); // Get pressed key
      if (ch != -1) {                   // If valid key value
        if ((ch & 0x10000) != 0) {      // Handle non-ASCII translated keys (Keypad 0 .. 9)
                                        // Bit  16:    non-ASCII bit (0 = ASCII, 1 = not ASCII)
                                        // Bits 15..8: modifiers (SHIFT, ALT, CTRL, GUI)
                                        // Bits  7..0: ASCII or HID key Usage ID if not ASCII
                                        // HID Usage ID values can be found in following link:
                                        // http://www.usb.org/developers/hidpage/Hut1_12v2.pdf
          ch &= 0xFF;                   // Remove non-ASCII bit and modifiers
          if ((ch>=0x59)&&(ch<=0x61)) { // Keypad 1 .. 9 key convert to
            ch = (ch - 0x59) + '1';     // ASCII  1 .. 9
          } else if (ch == 0x62) {      // Keypad 0 key convert to
            ch = '0';                   // ASCII  0
          } else {                      // If not Keypad 0 .. 9
            ch = -1;                    // invalidate the key
          }
        }
        if ((ch > 0) && (ch < 128)) {   // Output ASCII 0 .. 127 range
          putchar(ch);
          fflush(stdout);
        }
      }
    }
    osDelay(10U);
  }
}

/*----------------------------------------------------------------------------
  Thread 'Display': LCD display handler
 *---------------------------------------------------------------------------*/
static __NO_RETURN void Display (void *arg) {
  static uint8_t ip_addr[NET_ADDR_IP6_LEN];
  static char    ip_ascii[40];
  
  (void)arg;
	
  /* Print Link-local IPv6 address */
  netIF_GetOption (NET_IF_CLASS_ETH, netIF_OptionIP6_LinkLocalAddress, ip_addr, sizeof(ip_addr));

  netIP_ntoa(NET_ADDR_IP6, ip_addr, ip_ascii, sizeof(ip_ascii));

  while(1) {
    /* Wait for signal from DHCP */
    osThreadFlagsWait (0x01, osFlagsWaitAny, osWaitForever);

    /* Retrieve and print IPv4 address */
    netIF_GetOption (NET_IF_CLASS_ETH, netIF_OptionIP4_Address, ip_addr, sizeof(ip_addr));

    netIP_ntoa (NET_ADDR_IP4, ip_addr, ip_ascii, sizeof(ip_ascii));
  }
}

/*----------------------------------------------------------------------------
  Thread 'BlinkLed': Blink the LEDs on an eval board
 *---------------------------------------------------------------------------*/
static __NO_RETURN void BlinkLed (void *arg) {
  const uint8_t led_val[16] = { 0x48,0x88,0x84,0x44,0x42,0x22,0x21,0x11,
                                0x12,0x0A,0x0C,0x14,0x18,0x28,0x30,0x50 };
  uint32_t cnt = 0U;

  (void)arg;

  while(1) {
    /* Every 100 ms */
    LED_SetOut (led_val[cnt]);
    if (++cnt >= sizeof(led_val)) {
      cnt = 0U;
    }
    osDelay (100);
  }
}

/*----------------------------------------------------------------------------
  Main Thread 'app_main':  Application
 *---------------------------------------------------------------------------*/
__NO_RETURN void app_main (void *arg) {
  (void)arg;

#ifdef RTE_Compiler_EventRecorder
  EventRecorderInitialize(0U, 1U);
  EventRecorderEnable (EventRecordError, 0xC0U, 0xDCU);    /* Net Events     */
  EventRecorderEnable (EventRecordAll,   0xCAU, 0xCAU);    /* DHCP Events    */
  EventRecorderEnable (EventRecordAll,   0xD3U, 0xD3U);    /* HTTP Events    */
	
	EventRecorderEnable (EventRecordError, 0xB0U, 0xB5U);  /* USBH Error Events */
  EventRecorderEnable (EventRecordAll  , 0xB0U, 0xB0U);  /* USBH Core Events */
  EventRecorderEnable (EventRecordAll  , 0xB4U, 0xB4U);  /* USBH HID Events */
	
#endif

  LED_Initialize();
  
  if (finit ("M0:") == fsOK) {
    fmount ("M0:");
  }
  netInitialize ();
	balanza.pinSck = GPIO_PIN_14;
  balanza.pinData = GPIO_PIN_15;
  balanza.gpioSck = GPIOB;
  balanza.gpioData = GPIOB;
  balanza.gain = 128;
	HX711_Init(balanza);
	balanza = HX711_Tare(balanza,20);
	
	tara=balanza.offset;
								osThreadNew (CeldaDeCarga,NULL,NULL);
								osThreadNew (keyboard, NULL, NULL);
                //osThreadNew (BlinkLed, NULL, NULL);
  TID_Display = osThreadNew (Display,  NULL, NULL);

  while(1) {
    osThreadFlagsWait (0, osFlagsWaitAny, osWaitForever);
  }
}
