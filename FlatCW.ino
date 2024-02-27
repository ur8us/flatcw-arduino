#include <Wire.h>
#include "Adafruit_MPR121.h"

#include "TM1637.h"

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

#define TOUCH_LINES 36
#define TOUCH_TOOCLOSE 2
#define TOUCH_TOONARROW 2
#define DELTA_TOUCH 2 // 0 = disable
#define POS_NONE -1

#define TOUCH_FORGET_MS 5000
#define TOUCH_RENEW_DELTA 3 // 0 = disable

// Pins

#define BUZZ_OUT 2
#define BUZZ_OUT_I 3

#define KEY_OUT 4


boolean buzzPol = false;
volatile boolean buzzOn = false;

// Display TM1637

#define CLK 9//pins definitions for the module and can be changed to other ports       
#define DIO 8

TM1637 disp(CLK,DIO);

#define DISP_SPACE 17

// Settings

#define INIT_WPM 20
#define MIN_WPM 5
#define MAX_WPM 50
uint16_t speedWpm = INIT_WPM;

boolean reversePaddles = false;

int getDitTime() 
{
    return 1200/speedWpm;
}

int getDahTime() 
{
    return 3*1200/speedWpm;
}

int getPauseTime() 
{
    return 1200/speedWpm;
}

// Sound

void outputSetup()
{
  pinMode(BUZZ_OUT, OUTPUT);
  pinMode(BUZZ_OUT_I, OUTPUT);

  pinMode(KEY_OUT, OUTPUT);
  digitalWrite(KEY_OUT, LOW);      
}

void outputOn()
{
  buzzOn = true;
  digitalWrite(KEY_OUT, HIGH);      
}

void outputOff()
{
  buzzOn = false;
  digitalWrite(KEY_OUT, LOW);      
}

// Touch sensors

// You can have up to 4 on one i2c bus but one is enough for testing!
Adafruit_MPR121 cap5A = Adafruit_MPR121();
Adafruit_MPR121 cap5B = Adafruit_MPR121();
Adafruit_MPR121 cap5C = Adafruit_MPR121();

/*
// Keeps track of the last pins touched
// so we know when buttons are 'released'
uint16_t lasttouched = 0;
*/


// CW machine

volatile bool ditPressed = false, dahPressed = false, ditPriority = false;
bool ditPressedOld = false, dahPressedOld = false;

enum CWSTATES {CW_NONE, CW_SENDING_DIT, CW_SENDING_DAH, CW_PAUSE_AFTER_DIT, CW_PAUSE_AFTER_DAH };
CWSTATES cwState = CWSTATES::CW_NONE;

volatile boolean memDit = false, memDah = false;

int endTimer = 0;


void readEeprom()
{
  uint16_t signature = eeprom_read_word(0);

  if (signature == 0x4321) // Read if signature OK
  {
    speedWpm = eeprom_read_word(2);
    reversePaddles = eeprom_read_byte(4);
  }
  else // Set defaults
  {
    eeprom_write_word(0, 0x4321); // Write signature

    speedWpm = INIT_WPM;
    reversePaddles = false;

    writeEeprom();
  }
}

void writeEeprom()
{
  eeprom_update_word(2, speedWpm);
  eeprom_update_byte(4, reversePaddles);
}


void setup() 
{
  // EEPROM

  readEeprom();
    
  // Serial 
  
  Serial.begin(115200);


  while (!Serial) { // needed to keep leonardo/micro from starting too fast!
    delay(10);
  }


  Serial.println("Adafruit MPR121 Capacitive Touch sensor test");

  // Default address is 0x5A, if tied to 3.3V its 0x5B
  // If tied to SDA its 0x5C and if SCL then 0x5D
  if (!cap5A.begin(0x5A)) {
    Serial.println("0x5A - MPR121 not found, check wiring?");
    while (1);
  }
  
  if (!cap5B.begin(0x5B)) {
    Serial.println("0x5A - MPR121 not found, check wiring?");
    while (1);
  }
  
  if (!cap5C.begin(0x5C)) {
    Serial.println("0x5A - MPR121 not found, check wiring?");
    while (1);
  }

  Serial.println("MPR121 found!");

#define MPR121_TOUCH_THRESHOLD 6 // Defalut: 12
#define MPR121_RELEASE_THRESHOLD 3 // Defalut:  6

  cap5A.setThresholds(MPR121_TOUCH_THRESHOLD,MPR121_RELEASE_THRESHOLD);
  cap5B.setThresholds(MPR121_TOUCH_THRESHOLD,MPR121_RELEASE_THRESHOLD);
  cap5C.setThresholds(MPR121_TOUCH_THRESHOLD,MPR121_RELEASE_THRESHOLD);

  // Output

  outputSetup();

  // Display

  disp.set(BRIGHT_TYPICAL);//BRIGHT_TYPICAL = 2,BRIGHT_DARKEST = 0,BRIGHTEST = 7;
  disp.init(D4036B);

//return;  
  // Set up 1 kHz timer

  cli();//stop interrupts

//  //set timer0 interrupt at 1kHz
//  TCCR0A = 0;// set entire TCCR2A register to 0
//  TCCR0B = 0;// same for TCCR2B
//  TCNT0  = 0;//initialize counter value to 0
//  // set compare match register for 2khz increments
////  OCR0A = 124;// = (16*10^6) / (2000*64) - 1 (must be <256)
//  OCR0A = 249;// = (16*10^6) / (1000*64) - 1 (must be <256)
//  // turn on CTC mode
//  TCCR0A |= (1 << WGM01);
//  // Set CS01 and CS00 bits for 64 prescaler
//  TCCR0B |= (1 << CS01) | (1 << CS00);   
//  // enable timer compare interrupt
//  TIMSK0 |= (1 << OCIE0A);

//set timer2 interrupt at 1kHz
  TCCR2A = 0;// set entire TCCR2A register to 0
  TCCR2B = 0;// same for TCCR2B
  TCNT2  = 0;//initialize counter value to 0
  // set compare match register for 8khz increments
//  OCR2A = 249;// = (16*10^6) / (8000*8) - 1 (must be <256)
  OCR2A = 249;// = (16*10^6) / (1000*64) - 1 (must be <256)
  // turn on CTC mode
  TCCR2A |= (1 << WGM21);
  // Set CS22 bit for 64 prescaler
  TCCR2B |= (1 << CS22);   
  // enable timer compare interrupt
  TIMSK2 |= (1 << OCIE2A);

  sei();//allow interrupts
}

//int leftPosFirst = POS_NONE, leftPosSecond = POS_NONE, rightPosFirst = POS_NONE, rightPosSecond = POS_NONE;

int grLprev = POS_NONE, grRprev = POS_NONE, grCprev = POS_NONE;
int leftPos = POS_NONE, rightPos = POS_NONE, leftPosPrev = POS_NONE, rightPosPrev = POS_NONE;

//unsigned long lastPaddleMs = 0;

volatile unsigned int pauseTimer = 0;
  
void loop() 
{
  // Get current time

//  unsigned long curMs = millis(); 
  
  // Get the currently touched pads
  uint64_t currTouched = ((uint64_t)cap5A.touched()) | (((uint64_t)cap5B.touched())<<12) | (((uint64_t)cap5C.touched())<<24);

  if (currTouched & 1) // Setup
  {
    // Reset all states

    pauseTimer = TOUCH_FORGET_MS; // Forget
    leftPosPrev = POS_NONE;
    rightPosPrev = POS_NONE;

    int8_t dispData[] = {DISP_SPACE, DISP_SPACE, DISP_SPACE, DISP_SPACE,};

    if ( (currTouched & (1UL<<5)) || (currTouched & (1UL<<10)) ) // WPM
    {
      if (currTouched & (1UL<<5)) // Minus
      {
        speedWpm --;
        if (speedWpm < MIN_WPM)
          speedWpm = MIN_WPM;
      }
      else
      if (currTouched & (1UL<<10)) // Plus
      {
        speedWpm ++;
        if (speedWpm > MAX_WPM)
          speedWpm = MAX_WPM;
      }


      // Display WPM
      
      dispData[0] = 5;  // S
      dispData[1] = 25; // P
      dispData[2] = speedWpm / 10;
      dispData[3] = speedWpm%10;
  
      if (dispData[2] == 0)
        dispData[2] = DISP_SPACE;
      
      disp.point(true);
      disp.display(dispData);

      ditPressed = true; // Test

      writeEeprom();
      delay(200);
    }
    else
    if ( currTouched & (1UL<<20) ) // Reverse paddle
    {
      reversePaddles = !reversePaddles;

      // Display REV

      dispData[0] = 19;  // R
      dispData[1] = 0x0E; // E
      dispData[2] = 0; // O

      if (reversePaddles)
      {
        dispData[3] = 21; // n
      }
      else
      {
        dispData[3] = 0x0F; // f
      }
      
      disp.point(true);
      disp.display(dispData);

      writeEeprom();
      delay(1000);
    }
//    else
//    {
//      // Display SET
//
//      dispData[0] = 5;  // S
//      dispData[1] = 0x0E; // E
//      dispData[2] = 22; // t
//      dispData[2] = DISP_SPACE; 
//      
//      disp.point(false);
//      disp.display(dispData);
//
//    }
    
    
    return;
  }


  // Clear some lines

  currTouched &= ~(1ULL << (TOUCH_LINES-1) );
  currTouched &= ~(1ULL << (TOUCH_LINES-2) );
  currTouched &= ~(1ULL << (TOUCH_LINES-3) );

  currTouched &= ~(1ULL << 0 );
  currTouched &= ~(1ULL << 1 );
  currTouched &= ~(1ULL << 2 );

  // Calculate begin and end positions of left and right touches

  int grLbeg = POS_NONE, grLend = POS_NONE;

  for (int i=0; i<TOUCH_LINES; i++)
  {
      if (currTouched & (1ULL << i) )
      {
        if (grLbeg == POS_NONE)
            grLbeg = i;

        grLend = i;
      }
      else
      {
        if (grLbeg != POS_NONE)
          break;
      }
  }
  
  int grRbeg = POS_NONE, grRend = POS_NONE;

  for (int i=TOUCH_LINES-1; i>=0; i--)
  {
      if (currTouched & (1ULL << i) )
      {
        if (grRbeg == POS_NONE)
            grRbeg = i;

        grRend = i;
      }
      else
      {
        if (grRbeg != POS_NONE)
          break;
      }
  }

  // Calculated middle positions of left and right touches

  int grL = (grLbeg+grLend)/2; 
  int grR = (grRbeg+grRend)/2;

/*
  if ( abs(grLend - grLbeg) <= TOUCH_TOONARROW) // Too narrow?
    grL = POS_NONE;

  if ( abs(grRend - grRbeg) <= TOUCH_TOONARROW) // Too narrow?
    grR = POS_NONE;
*/

  if ( abs (grL-grR) <=  TOUCH_TOOCLOSE) // Are the touches too close?
  {
    grL = grR = (grL+grR)/2;
  }


//  Serial.println(-grLbeg+grLend);


//  Serial.print("grL: ");
//  Serial.print(grL);
//  Serial.print(" grR: ");
//  Serial.print(grR);
//
//  Serial.println("");

  bool leftPriority = false;

  // Analyze touches grL and grR, decide if dit or dah is pressed

  if ( (grL == POS_NONE) && (grR == POS_NONE) ) // No touch
  {
    leftPos = POS_NONE;
    rightPos = POS_NONE;
    grCprev = POS_NONE;
  }
  else
  if ( grL == grR )  // One single touch
  {

    if ( (leftPos == POS_NONE) && (rightPos == POS_NONE) ) // If left and righ touches are not already defined
    {
    
      if (grCprev==POS_NONE) // We need to fix the first touch
      {
        grCprev = grL; // = grR
      }
      else
      {
        if (DELTA_TOUCH>0) // Need to swipe to start
        {      
          if ( (TOUCH_RENEW_DELTA>0) && (pauseTimer<TOUCH_FORGET_MS) ) // Try to use the renew timer to remember last pos
          {
            if (abs(grL-leftPosPrev) < TOUCH_RENEW_DELTA)
              leftPos = grL;
            else   
            if (abs(grR-rightPosPrev) < TOUCH_RENEW_DELTA)
              rightPos = grR;
            else            
            {
              pauseTimer = TOUCH_FORGET_MS; // Forget
              leftPosPrev = POS_NONE;
              rightPosPrev = POS_NONE;
            }
          }
          
          if ( (grL - grCprev) >= DELTA_TOUCH )
          {
            leftPos = grL;
          }
          else
          if ( (grCprev - grR) >= DELTA_TOUCH )
          {
            rightPos = grR;
          }

//          if ( (leftPos==POS_NONE) && (rightPos==POS_NONE) )
//          {
//            pauseTimer = TOUCH_FORGET_MS; // Forget
//          }
        }
        else // Do not need to swipe, just make choise depending on a position
        {
          if ( grL >= TOUCH_LINES/2 )
            rightPos = grR;
          else
            leftPos = grL;            
        }
      }
      
    }
    else
    if ( (leftPos != POS_NONE) && (rightPos != POS_NONE) ) // If left and righ touches are both defined
    {
      if ( abs (grL - leftPos) < abs (grR - rightPos) ) // Decide which direction was realeased
      {
        leftPos = grL; // Update
        rightPos = POS_NONE; // Delete
      }
      else
      {
        rightPos = grR; // Update
        leftPos = POS_NONE; // Delete
      }
    }
    else // One of touches defined, update positions
    {
      if (leftPos != POS_NONE)
        leftPos = grL;
      else
      if (rightPos != POS_NONE)
        rightPos = grR;
    }
          
  }
  else
  // if ( grlL != grR ) // Two different touches
  {

    // Update positions

    if ( (leftPos == POS_NONE) && (rightPos == POS_NONE) && (grCprev != POS_NONE) ) // If left and righ touches are not already defined
    {
      if ( abs (grL - grCprev) < abs (grR - grCprev) ) // Decide which direction was touched first 
        leftPriority = true;
      else
        leftPriority = false;
    }

    leftPos = grL;
    rightPos = grR;

  }

  if (leftPos != POS_NONE)
    leftPosPrev = leftPos;

  if (rightPos != POS_NONE)
    rightPosPrev = rightPos;

//  Serial.print("leftPos: ");
//  Serial.print(leftPos);
//
//  Serial.print(" rightPos: ");
//  Serial.print(rightPos);
//
//  Serial.println("");

  ditPressed = reversePaddles ? (rightPos != POS_NONE) : (leftPos != POS_NONE);
  dahPressed = reversePaddles ? (leftPos != POS_NONE) : (rightPos != POS_NONE);

  ditPriority = leftPriority ^ reversePaddles;

  if (ditPressed && (!ditPressedOld) )
    memDit = true;
  if (dahPressed && (!dahPressedOld) )
    memDah = true;

//  if ( ditPressed || dahPressed ) // Update millisecond timestamp since last touch
//      lastPaddleMs = curMs;

//  Serial.println(lastPaddleMs);

  ditPressedOld = ditPressed;
  dahPressedOld = dahPressed;


  if (ditPressed || dahPressed)
    pauseTimer = 0;

  delay(1);

  
#if 0

  // Calculate persistent positions of left and right touches

  int leftPos = POS_NONE, rightPos = POS_NONE;

  if (grL != POS_NONE)
  {
    if (leftPos == -1) 
    {
      if (grL < TOUCH_LINES/2)
        leftPos = grL;
    }
    else
    {
      if (abs (leftPos-grL) <= DELTA_TOUCH )
        leftPos = grL;
      else      
        leftPos = POS_NONE;
    }
  }
  else
    leftPos = POS_NONE;

  if (grR != POS_NONE)
  {
    if (rightPos == -1) 
    {
      if (grR >= TOUCH_LINES/2)
        rightPos = grR;
    }
    else
    {
      if (abs (rightPos-grR) <= DELTA_TOUCH )
        rightPos = grR;
      else      
        rightPos = POS_NONE;
    }
  }
  else
    rightPos = POS_NONE;



  Serial.print("leftPos: ");
  Serial.print(leftPos);

  Serial.print(" rightPos: ");
  Serial.print(rightPos);

  Serial.println("");
  
  delay(1);

  ditPressed = reversePaddles ? (rightPos != POS_NONE) : (leftPos != POS_NONE);
  dahPressed = reversePaddles ? (leftPos != POS_NONE) : (rightPos != POS_NONE);
#endif  

#if 0
  // Analyze movements

  if (grL != POS_NONE)
  {
    if (leftPosFirst == -1) 
    {
        leftPosFirst = grL;
        leftPosSecond = grL;
    }
    else
    {
      if (grL > leftPosSecond)
      {
          leftPosSecond = grL;

//          if (grL == grR)
//          {
//            rightPosFirst = POS_NONE;
//            rightPosSecond = POS_NONE;
//          }
      }
    }
  }
  else
  {
    leftPosFirst = POS_NONE;
    leftPosSecond = POS_NONE;
  }


  if (grR != POS_NONE)
  {
    if (rightPosFirst == -1) 
//    if ( (rightPosFirst == -1) || (grL != grR) ) 
    {
        rightPosFirst = grR;
        rightPosSecond = grR;
    }
    else
    {
      if (grR < rightPosSecond)
      {
          rightPosSecond = grR;

//          if (grL == grR)
//          {
//            leftPosFirst = POS_NONE;
//            leftPosSecond = POS_NONE;
//          }      
      }
    }
  }
  else
  {
    rightPosFirst = POS_NONE;
    rightPosSecond = POS_NONE;
  }

  Serial.print("leftPosFirst: ");
  Serial.print(leftPosFirst);

  Serial.print(" leftPosSecond: ");
  Serial.print(leftPosSecond);

  Serial.print(" rightPosFirst: ");
  Serial.print(rightPosFirst);

  Serial.print(" rightPosSecond: ");
  Serial.print(rightPosSecond);

  Serial.println("");



  ditPressed = (leftPosSecond - leftPosFirst) >= DELTA_TOUCH;
  dahPressed = (rightPosFirst - rightPosSecond) >= DELTA_TOUCH;

#endif  
//
//  if ( ditPressed && (grL==grR) )
//  {
//    if (!dahPressed)
//    {
//        rightPosFirst = POS_NONE;
//        rightPosSecond = POS_NONE;
//    }
//  }
//
//  if ( dahPressed && (grL==grR) )
//  {
//    if (!ditPressed)
//    {
//        leftPosFirst = POS_NONE;
//        leftPosSecond = POS_NONE;
//    }
//  }

/*
  return;

  for (uint8_t i = 0; i < 12; i++) {
    // it if *is* touched and *wasnt* touched before, alert!
    if ((currTouched & _BV(i)) && !(lasttouched & _BV(i)) ) {
      Serial.print(i); Serial.println(" touched");
    }
    // if it *was* touched and now *isnt*, alert!
    if (!(currTouched & _BV(i)) && (lasttouched & _BV(i)) ) {
      Serial.print(i); Serial.println(" released");
    }
  }

  // reset our state
  lasttouched = currTouched;

  // comment out this line for detailed data from the sensor!
  return;

  // debugging info, what
  Serial.print("\t\t\t\t\t\t\t\t\t\t\t\t\t 0x"); Serial.println(cap.touched(), HEX);
  Serial.print("Filt: ");
  for (uint8_t i = 0; i < 12; i++) {
    Serial.print(cap.filteredData(i)); Serial.print("\t");
  }
  Serial.println();
  Serial.print("Base: ");
  for (uint8_t i = 0; i < 12; i++) {
    Serial.print(cap.baselineData(i)); Serial.print("\t");
  }
  Serial.println();

  // put a delay so it isn't overwhelming
  delay(100);
*/  
}

//storage variables
//boolean toggle0 = 0;

// 1 kHz timer

ISR(TIMER2_COMPA_vect)
//ISR(TIMER0_COMPA_vect) 
{ 
//  return;
////generates pulse wave of frequency 2kHz/2 = 1kHz (takes two cycles for full wave- toggle high then toggle low)
//  if (toggle0){
//    digitalWrite(BUZZ,HIGH);
//    toggle0 = 0;
//  }
//  else{
//    digitalWrite(BUZZ,LOW);
//    toggle0 = 1;
//  }

//  currTime++;

  // Buzzer

  if ( buzzOn )
    {
      if (buzzPol)
      {
          digitalWrite(BUZZ_OUT, HIGH);      
          digitalWrite(BUZZ_OUT_I, LOW);      
      }
      else
      {
          digitalWrite(BUZZ_OUT_I, HIGH);      
          digitalWrite(BUZZ_OUT, LOW);      
      }

      buzzPol = !buzzPol;
    }


  // Timers

  pauseTimer++;

  if (endTimer)
    endTimer--;

  // State machine

  switch (cwState) {
  
      case CWSTATES::CW_NONE:

          if (ditPriority)
          {
            if (ditPressed) {
                endTimer = getDitTime();
                outputOn();
                memDit = false;
                cwState = CWSTATES::CW_SENDING_DIT;
            }
            else
            if (dahPressed) {
                endTimer = getDahTime();
                outputOn();
                memDah = false;
                cwState = CWSTATES::CW_SENDING_DAH;
            }
          }
          else
          {
            if (dahPressed) {
                endTimer = getDahTime();
                outputOn();
                memDah = false;
                cwState = CWSTATES::CW_SENDING_DAH;
            }
            else
            if (ditPressed) {
                endTimer = getDitTime();
                outputOn();
                memDit = false;
                cwState = CWSTATES::CW_SENDING_DIT;
            }

          }
          break;
  
      case CWSTATES::CW_SENDING_DIT:
          if (endTimer == 0) {
              endTimer = getPauseTime();
              outputOff();
              cwState = CWSTATES::CW_PAUSE_AFTER_DIT;
          }
          break;
  
      case CWSTATES::CW_SENDING_DAH:
          if (endTimer == 0) {
              endTimer = getPauseTime();
              outputOff();
              cwState = CWSTATES::CW_PAUSE_AFTER_DAH;
          }
          break;
  
      case CWSTATES::CW_PAUSE_AFTER_DIT:
          if (endTimer == 0) {
              if (dahPressed || memDah) {
                  endTimer = getDahTime();
                  outputOn();
                  memDah = false;
                  cwState = CWSTATES::CW_SENDING_DAH;
              }
              else if (ditPressed || memDit) {
                  endTimer = getDitTime();
                  outputOn();
                  memDit = false;
                  cwState = CWSTATES::CW_SENDING_DIT;
              }
              else
                  cwState = CWSTATES::CW_NONE;
          }
          break;
  
      case CWSTATES::CW_PAUSE_AFTER_DAH:
          if (endTimer == 0) {
              if (ditPressed || memDit) {
                  endTimer = getDitTime();
                  outputOn();
                  memDit = false;
                  cwState = CWSTATES::CW_SENDING_DIT;
              }
              else if (dahPressed || memDah) {
                  endTimer = getDahTime();
                  outputOn();
                  memDah = false;
                  cwState = CWSTATES::CW_SENDING_DAH;
              }
              else
                  cwState = CWSTATES::CW_NONE;
          }
          break;
  }

}
