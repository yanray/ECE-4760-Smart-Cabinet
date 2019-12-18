/*
 * File:        TFT, keypad, DAC, LED, PORT EXPANDER test
 *              With serial interface to PuTTY console
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 * http://people.ece.cornell.edu/land/courses/ece4760/PIC32/target_board.html
 * Target PIC:  PIC32MX250F128B
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!.................................
//  TEST OLD CODE WITH NEW THREADS
//#include "config_1_2_3.h"
#include "config_1_3_2.h"
// threading library
//#include "pt_cornell_1_2_3.h"
#include "pt_cornell_1_3_2.h"
// yup, the expander
#include "port_expander_brl4.h"

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
// need for sin function
#include <stdfix.h>
#include <math.h>
////////////////////////////////////

// lock out timer interrupt during spi comm to port expander
// This is necessary if you use the SPI2 channel in an ISR
#define start_spi2_critical_section INTEnable(INT_T2, 0);
#define end_spi2_critical_section INTEnable(INT_T2, 1);

////////////////////////////////////
// some precise, fixed, short delays
// to use for extending pulse durations on the keypad
// if behavior is erratic
#define NOP asm("nop");
// 1/2 microsec
#define wait20 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;
// one microsec
#define wait40 wait20;wait20;
////////////////////////////////////

// string buffer
char buffer[60];

// === thread structures ============================================
// thread control structs
static struct pt pt_timer, pt_motor, pt_command, pt_key;

// system 1 second interval tick
int sys_time_seconds ;

volatile int pick_up_code[2] = {0, 0};
volatile int code_matching = 0;
volatile int enter_num = 0;
volatile int code_display_pos_x;

// motor status
#define IDLE 0
#define MOVE_IN 1
#define MOVE_OUT 2
volatile int Motor_Status = IDLE;

// current motor
#define IDLE 0
#define MOTOR1 1 //motor for third layer
#define MOTOR2 2//motor for second layer
#define MOTOR3 3 //motor for platform
volatile int Motor_Num = IDLE;

// current cabinet status
#define EMPTY 0
#define FULL 1
#define STORING 2
#define TAKING 3
volatile int Cell1_Status = EMPTY;
volatile int Cell2_Status = EMPTY;

// system status
#define IDLE 0
#define Store_To_Cell_One 1
#define Store_To_Cell_Two 2
#define Take_From_Cell_One 3
#define Take_From_Cell_Two 4
#define TEST 5
volatile int System_Status = IDLE;

// remaining time of current process
volatile int remaining_time;

volatile int time;

#define int2Accum(a) ((_Accum)(a))

// === print a line on TFT =====================================================
// print a line on the TFT
// string buffer
char buffer[60];
void printLine(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 10 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 8, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(1);
    tft_writeString(print_buffer);
}

void printLine2(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 20 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 16, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(2);
    tft_writeString(print_buffer);
}

// === Timer Thread =================================================
static PT_THREAD (protothread_timer(struct pt *pt))
{
    static int begin_time;
    PT_BEGIN(pt);
    // set up LED to blink
    mPORTASetBits(BIT_0 );	//Clear bits to ensure light is off.
    mPORTASetPinsDigitalOut(BIT_0 );    //Set port as output
     
    sprintf(buffer,"Smart Cabinet System");
    printLine2(1, buffer, ILI9340_WHITE, ILI9340_BLACK);

    tft_drawRoundRect(10,50,220,160,5,ILI9340_WHITE);
    tft_drawLine(10,130,230,130,ILI9340_WHITE);
    
    sprintf(buffer,"CELL1");
    tft_fillRoundRect(30, 70, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
    tft_setTextColor(ILI9340_WHITE); 
    tft_setCursor(30, 70);
    tft_setTextSize(2);
    tft_writeString(buffer);
    
    sprintf(buffer,"CELL2");
    tft_fillRoundRect(30, 150, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
    tft_setTextColor(ILI9340_WHITE); 
    tft_setCursor(30, 150);
    tft_setTextSize(2);
    tft_writeString(buffer);
    
    sprintf(buffer,"System Status:");
    printLine2(11, buffer, ILI9340_WHITE, ILI9340_BLACK);

    
     while(1) {
        // yield time 1 second
        begin_time = PT_GET_TIME();
        
        // toggle the LED on the big board
        mPORTAToggleBits(BIT_0);

        // draw the status of cell 1 and 2
        if (Cell1_Status == EMPTY){
            sprintf(buffer,"EMPTY");
        }
        else if (Cell1_Status == FULL){
            sprintf(buffer,"FULL");
        }
        else if (Cell1_Status == STORING){
            sprintf(buffer,"STORING");
        }
        else if (Cell1_Status == TAKING){
            sprintf(buffer,"TAKING");
        }
        tft_fillRoundRect(70, 90, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setTextColor(ILI9340_WHITE); 
        tft_setCursor(70, 90);
        tft_setTextSize(2);
        tft_writeString(buffer);
        
        if (Cell2_Status == EMPTY){
            sprintf(buffer,"EMPTY");
        }
        else if (Cell2_Status == FULL){
            sprintf(buffer,"FULL");
        }
        else if (Cell2_Status == STORING){
            sprintf(buffer,"STORING");
        }
        else if (Cell2_Status == TAKING){
            sprintf(buffer,"TAKING");
        }
        tft_fillRoundRect(70, 170, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setTextColor(ILI9340_WHITE); 
        tft_setCursor(70, 170);
        tft_setTextSize(2);
        tft_writeString(buffer);
        
        // draw current system status
        if (System_Status == IDLE){
            sprintf(buffer,"IDLE");
        }
        else if (System_Status == Store_To_Cell_One | System_Status == Store_To_Cell_Two){
            sprintf(buffer,"Storing");
        }
        else if (System_Status == Take_From_Cell_One | System_Status == Take_From_Cell_Two){
            sprintf(buffer,"Taking");
        }
        else if (System_Status == TEST){
            sprintf(buffer,"Testing");
        }
        tft_fillRoundRect(70, 240, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setTextColor(ILI9340_WHITE); 
        tft_setCursor(70, 240);
        tft_setTextSize(2);
        tft_writeString(buffer);
        
        
        PT_YIELD_TIME_msec(100 - (PT_GET_TIME() - begin_time));
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // timer thread


// === Command Thread =================================================
static PT_THREAD (protothread_command(struct pt *pt))
{
    PT_BEGIN(pt);
    static int i;
    while(1) {
        if (System_Status == TEST){
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<54;i++) PT_YIELD_TIME_msec(1000);
            System_Status = IDLE;
            Motor_Num = IDLE;
            Motor_Status = IDLE;
        }
        if (System_Status == Store_To_Cell_One){
            Cell1_Status = STORING;
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_IN;
            for (i=0;i<26+8;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR1;
            Motor_Status = MOVE_OUT;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<8;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR1;
            Motor_Status = MOVE_IN;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<26;i++) PT_YIELD_TIME_msec(1000);
            
            System_Status = IDLE;
            Motor_Num = IDLE;
            Motor_Status = IDLE;
            Cell1_Status = FULL;
        }
        if (System_Status == Store_To_Cell_Two){
            Cell2_Status = STORING;
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_IN;
            for (i=0;i<26+28+8;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR2;
            Motor_Status = MOVE_OUT;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<8;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR2;
            Motor_Status = MOVE_IN;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<54;i++) PT_YIELD_TIME_msec(1000);
            
            System_Status = IDLE;
            Motor_Num = IDLE;
            Motor_Status = IDLE;
            Cell2_Status = FULL;
        }
        if (System_Status == Take_From_Cell_One){
            Cell1_Status = TAKING;
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_IN;
            for (i=0;i<26;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR1;
            Motor_Status = MOVE_OUT;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_IN;
            for (i=0;i<8;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR1;
            Motor_Status = MOVE_IN;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<26+8;i++) PT_YIELD_TIME_msec(1000);
            
            System_Status = IDLE;
            Motor_Num = IDLE;
            Motor_Status = IDLE;
            Cell1_Status = EMPTY;
        }
        if (System_Status == Take_From_Cell_Two){
            Cell2_Status = TAKING;
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_IN;
            for (i=0;i<26+28;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR2;
            Motor_Status = MOVE_OUT;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_IN;
            for (i=0;i<8;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR2;
            Motor_Status = MOVE_IN;
            for (i=0;i<60;i++) PT_YIELD_TIME_msec(1000);
            Motor_Num = MOTOR3;
            Motor_Status = MOVE_OUT;
            for (i=0;i<26+28+8;i++) PT_YIELD_TIME_msec(1000);
            
            System_Status = IDLE;
            Motor_Num = IDLE;
            Motor_Status = IDLE;
            Cell2_Status = EMPTY;
        }
        
        PT_YIELD_TIME_msec(1000);
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// === Motor Thread =============================================
static PT_THREAD (protothread_motor(struct pt *pt))
{
    PT_BEGIN(pt);
    
    while(1) {
        // yield time
        switch (Motor_Status)
        {
            case IDLE: 
                PT_YIELD_TIME_msec(10);
                break;
                
            case MOVE_IN:
                start_spi2_critical_section;
                clearBits(GPIOY, 0x01);
                end_spi2_critical_section ;
                while (Motor_Status == MOVE_IN){
                    switch (Motor_Num)
                    {
                        case IDLE:
                            PT_YIELD_TIME_msec(10);
                            break;
                        case MOTOR1:
                            writePE(GPIOZ, 0x01);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x02);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x04);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x08);
                            PT_YIELD_TIME_msec(2);
                            break;
                        case MOTOR2:
                            writePE(GPIOZ, 0x10);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x20);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x40);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x80);
                            PT_YIELD_TIME_msec(2);
                            break;
                        case MOTOR3:
                            setBits(GPIOY, 0x02);
                            PT_YIELD_TIME_msec(5);
                            clearBits(GPIOY, 0x02);
                            PT_YIELD_TIME_msec(5);
                            break;
                    }
                }
                break;
                
            case MOVE_OUT:
                setBits(GPIOY, 0x01);
                while (Motor_Status == MOVE_OUT){
                    switch (Motor_Num)
                    {
                        case IDLE:
                            PT_YIELD_TIME_msec(10);
                            break;
                        case MOTOR1:
                            writePE(GPIOZ, 0x08);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x04);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x02);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x01);
                            PT_YIELD_TIME_msec(2);
                            break;
                        case MOTOR2:
                            writePE(GPIOZ, 0x80);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x40);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x20);
                            PT_YIELD_TIME_msec(2);
                            writePE(GPIOZ, 0x10);
                            PT_YIELD_TIME_msec(2);
                            break;
                        case MOTOR3:
                            setBits(GPIOY, 0x02);
                            PT_YIELD_TIME_msec(5);
                            clearBits(GPIOY, 0x02);
                            PT_YIELD_TIME_msec(5);
                            break;
                    }
                }
                break;
        }
                
        // NEVER exit while
    } // END WHILE(1)
    PT_END(pt);
} // Motor thread

static int seed = 0;
_Accum random_Num(_Accum lower_bound, _Accum upper_bound)
{
    int randomNum = rand() % 1000 + 1;
    
    return int2Accum(randomNum) / 1000 * (upper_bound - lower_bound) + lower_bound;
}


// === Key Thread =============================================
static PT_THREAD (protothread_key(struct pt *pt))
{
    PT_BEGIN(pt);
    static int key_value;

#define ENTER BIT_6
#define RIGHT BIT_5
#define DOWN BIT_4
#define UP BIT_3
#define LEFT BIT_2
    
      while(1) {
        // yield time
        PT_YIELD_TIME_msec(10);
        
        key_value  = readBits(GPIOY, ENTER);
        if(key_value==0) {
            while(1){
                PT_YIELD_TIME_msec(10);
                key_value  = readBits(GPIOY, ENTER);
                if (key_value != 0){
                    code_matching *= 10;
                    code_matching += enter_num;
                    code_display_pos_x += 15;
                    enter_num = 0;
                    
                    if(code_matching < 100)
                    {
                        sprintf(buffer,"%d", enter_num);

                        tft_fillRoundRect(code_display_pos_x, 260, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
                        tft_setTextColor(ILI9340_WHITE); 
                        tft_setCursor(code_display_pos_x, 260);
                        tft_setTextSize(2);
                        tft_writeString(buffer);
                        
                        sprintf(buffer,"Password: %d", code_matching);
                        printLine2(15, buffer, ILI9340_WHITE, ILI9340_BLACK);
                    }
                    else
                    {
                        if(code_matching == pick_up_code[0])
                        {
                            sprintf(buffer,"Correct, Waiting...");
                            printLine2(15, buffer, ILI9340_WHITE, ILI9340_BLACK);
                            
                            System_Status = Take_From_Cell_One;
                        }
                        else if (code_matching == pick_up_code[1])
                        {
                            sprintf(buffer,"Correct, Waiting...");
                            printLine2(15, buffer, ILI9340_WHITE, ILI9340_BLACK);
                                    
                            System_Status = Take_From_Cell_Two;
                        }
                        else
                        {
                            sprintf(buffer,"Sorry, Wrong Code");
                            printLine2(15, buffer, ILI9340_WHITE, ILI9340_BLACK);
                        }
                            
                    }
                    
                    break;
                }
            }
        }

        key_value  = readBits(GPIOY, UP);
        if(key_value==0) {
            while(1){
                PT_YIELD_TIME_msec(10);
                key_value  = readBits(GPIOY, UP);
                if (key_value != 0){
                    if(Cell1_Status == EMPTY)
                    {
                        pick_up_code[0] = random_Num(100, 999);
                        sprintf(buffer,"Pick Code: %d",pick_up_code[0]);
                        printLine2(13, buffer, ILI9340_WHITE, ILI9340_BLACK);
                        
                        System_Status = Store_To_Cell_One;
                    }
                    else if(Cell2_Status == EMPTY)
                    {
                        pick_up_code[1] = random_Num(100, 999);
                        sprintf(buffer,"Pick Code: %d",pick_up_code[1]);
                        printLine2(13, buffer, ILI9340_WHITE, ILI9340_BLACK);
                        
                        System_Status = Store_To_Cell_Two;
                    }
                    else
                    {
                        sprintf(buffer,"Sorry, it's full");
                        printLine2(13, buffer, ILI9340_WHITE, ILI9340_BLACK);
                    }
                    break;
                }
            }
        }
        
        key_value  = readBits(GPIOY, DOWN);
        if(key_value==0) {
            while(1){
                PT_YIELD_TIME_msec(10);
                key_value  = readBits(GPIOY, DOWN);
                if (key_value != 0){
                    enter_num = 0;
                    code_matching = 0;
                    code_display_pos_x = 140;
                    
                    sprintf(buffer,"Enter Code: %d", enter_num);
                    printLine2(13, buffer, ILI9340_WHITE, ILI9340_BLACK);
                    
                    
                    break;
                }
            }
        }
        
        key_value  = readBits(GPIOY, LEFT);
        if(key_value==0) {
            while(1){
                PT_YIELD_TIME_msec(10);
                key_value  = readBits(GPIOY, LEFT);
                if (key_value != 0){
                    enter_num--;
                    if(enter_num < 0)
                    {
                        enter_num += 10;
                    }
                    sprintf(buffer,"%d", enter_num);
                    
                    tft_fillRoundRect(code_display_pos_x, 260, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
                    tft_setTextColor(ILI9340_WHITE); 
                    tft_setCursor(code_display_pos_x, 260);
                    tft_setTextSize(2);
                    tft_writeString(buffer);
                    
                    //System_Status = TEST;
                    break;
                }
            }
        }
        
        key_value  = readBits(GPIOY, RIGHT);
        if(key_value==0) {
            while(1){
                PT_YIELD_TIME_msec(10);
                key_value  = readBits(GPIOY, RIGHT);
                if (key_value != 0){
                    enter_num++;
                    if(enter_num >= 10)
                    {
                        enter_num -= 10;
                    }
                    sprintf(buffer,"%d", enter_num);
                    
                    tft_fillRoundRect(code_display_pos_x, 260, 100, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
                    tft_setTextColor(ILI9340_WHITE); 
                    tft_setCursor(code_display_pos_x, 260);
                    tft_setTextSize(2);
                    tft_writeString(buffer);
                    
                    break;
                }
            }
        }
        
     }

        // NEVER exit while
   // END WHILE(1)
    PT_END(pt);
} // key thread


// === Main  ======================================================
void main(void) {
  ANSELA = 0; ANSELB = 0; 
   
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();
  
  // === set up i/o port pin ===============================
    mPORTBSetPinsDigitalOut(BIT_0 );    //Set port as output
    initPE();
    mPortZSetPinsOut(BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4 | BIT_5 | BIT_6 | BIT_7);
    mPortYSetPinsOut(BIT_0 | BIT_1);
    mPortYSetPinsIn(BIT_2 | BIT_3 | BIT_4 | BIT_5 | BIT_6);
    mPortYEnablePullUp(BIT_2 | BIT_3 | BIT_4 | BIT_5 | BIT_6);

  // init the threads
  PT_INIT(&pt_timer);
  PT_INIT(&pt_motor);
  PT_INIT(&pt_command);
  PT_INIT(&pt_key);
  

  // init the display
  // NOTE that this init assumes SPI channel 1 connections
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(0); // Use tft_setRotation(1) for 320x240

  // seed random color
  srand(1);
  
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
      PT_SCHEDULE(protothread_motor(&pt_motor));
      PT_SCHEDULE(protothread_command(&pt_command));
      PT_SCHEDULE(protothread_key(&pt_key));  
      
      }
  } // main

// === end  ======================================================
