/***************************************************************
 * All interrupts need to be turned off to maintain clock accuracy
 * This includes all Serial.print* commands
 */

#include <Time.h>
#include <TimeLib.h>
#include <FastLED.h>

#define LED_PIN 3
#define LED_TYPE WS2812
#define COLOR_ORDER GRB
#define NUM_LEDS 16

CRGB leds[NUM_LEDS];

const byte inputPin = 2;       // Mode switch
const byte maxObjects = 19;   // Number of objects identified below
/***
 * 00 - Mushroom2   Random
 * 01 - Near Bush2   Firefly Yellow  CHSV (64,255,X)
 * 02 - Door2       Soft white      CHSV (50,235,150)
 * 03 - Window2     Soft white      CHSV (50,235,60)
 * 04 - Mushroom3   Random
 * 05 - Far Bush3   Firefly Yellow  CHSV (64,255,X)
 * 06 - Near Bush1  Firefly Yellow  CHSV (64,255,X)
 * 07 - Mushroom1   Match mode
 * 08 - Door1       Soft white      CHSV (50,235,150)
 * 09 - Window1     Soft white      CHSV (50,235,60)
 * 10 - Sky1
 * 11 - Sky2
 * 12 - Sky3
 * 13 - Sky4
 * 14 - Sky5
 * 15 - Sky6
 * 16 - Light sensor
 * 17 - Serial input check (set time)
 * 18 - Mode switch   Switch must be the last object in the list
 * 
 * Object ID = 0-16
 */
 const byte modeSwitch=18;   // What ID number is the mode switch?
 const byte lightSensor=16;   // What ID number is the light sensor?
 const int lightSensorPin=A0;   // Light sensor pin
 float brightnessMultiplier = 1.00;   // Multiplier for brightness (V) values

//  Global color setup
const byte sunH = 64;
const byte sunS = 80;
const byte sunV = 255;
const byte moonH = 150;
const byte moonS = 0;
const byte moonV = 100;
const byte dayH = 64;
const byte dayS = 80;
const byte dayV = 190;
const byte nightH = 150;
const byte nightS = 255;
const byte nightV = 70;
const byte riseSetLowH = 0;
const byte riseSetHighH = 255;
const byte riseSetS = 180;
const byte riseSetV = 100;
const byte fireflyH = 64;
const byte fireflyS = 255;
byte fireflyV[3] = {0,0,0};
const byte houseH = 50;
const byte houseS = 235;
const byte houseV [10] = {0, 0, 100,80, 0, 0, 0, 0, 150, 100};   // Assign only for LEDs 2-3 & 8-9

byte timeMode = 0;
 /***
  * 0 = Sunrise     6a - 7a(1h)    3,600,000ms  Red
  * 1 = Morning     7a - 9a(2h)   10,800,000ms  Yellow
  * 2 = Midday      9a - 5p(8h)   25,200,000ms  White
  * 3 = Afternoon   5p - 7p(2h)   10,800,000ms  Orange
  * 4 = Sunset      7p - 8p(1h)    3,600,000ms  Pink
  * 5 = Evening     8p - 10p(2h)   7,200,000ms  Blue
  * 6 = Night       10p - 6a(8h)  28,800,000ms  Purple
  * 
  * 1h = 3,600,000ms
  */
const byte timeModeMax = 6;
byte currentHour = 0;
const long timeModeInterval []={6,7,9,17,19,20,22}; //  Starting hour of each timeMode

unsigned int  houseDMin = 70;    // Flicker duration minimum
unsigned int houseDMax = 120;   // Flicker duration maximum
int houseVHigh = 5;    // Flicker high brightness add
int houseVLow = -5;    // Flicker low brightness subtract
byte masterCandlesOn = 0;   // Switch all candle routines on/off

const unsigned int fireflyD = 600;    // Flicker duration 
const byte fireflyBrightness = 50;    // Firefly brightness
const unsigned int fireflyDelayMin = 1000;   // Firefly dark delay minimum
const unsigned int fireflyDelayMax = 4000;   // Firefly dark delay maximum
byte masterFireflyOn = 0;   // Switch all candle routines on/off

byte skyPosition=15;
byte colorChangeStep=0;   // Counter for 100 transitional steps
int vGoal = 0;           // Value goal for calculation over multiple time modes
unsigned long millisPerStep=0;
unsigned long nextObjectInterval [maxObjects];    // Next interval assignment for objects
unsigned long previousObjectMillis [maxObjects];        // Previous time of change for objects
unsigned long currentMillis = 1;    // Do not allow 0 millis
unsigned int speedFactor = 1;   // Set speed factor for testing.  1 = realtime, 10 = 10x speed, 1000 = 1000x speed


void setup() {
  delay(2000); // sanity delay
  Serial.begin(9600);
  FastLED.addLeds<LED_TYPE,LED_PIN,COLOR_ORDER>(leds,NUM_LEDS);
  pinMode(inputPin, INPUT_PULLUP);        // Set up mode switch
  randomSeed(analogRead(0));              // Set up seed for random numbers
  setTime(6,0,0,1,1,20);   // Set time/date to 6am 1 Jan 2020
  nextObjectInterval[modeSwitch] = 500;   // Set button debounce to 500ms
  nextObjectInterval[lightSensor] = 500;   // Set light sensor check to 500ms
  nextObjectInterval[modeSwitch] = 500;   // Set button debounce to 500ms
  setInitialColors();   // set start-up colors
  speedFactor = constrain(speedFactor, 1, 10000);    // Make sure speedFactor is set to a valid value 1 - 10,000
  
  // Everything ready
  //Serial.println("System initialized");
}

//###################################

void loop() 
{    
    
  // Set current time
  if (millis() == 0) delay (2);
  currentMillis = millis(); 
  currentHour = hour();
  
  // Check if mode switch press needs to force a mode change (debounced)
  if (digitalRead(inputPin)== 0 && (currentMillis - previousObjectMillis[modeSwitch]) > nextObjectInterval[modeSwitch]) forceTimeModeChange();

  // Check if time mode change is needed
  if (timeMode < 6 && hour() >= timeModeInterval[timeMode+1]) timeModeChange();
  else if (timeMode == 6 && hour() == timeModeInterval [0]) timeModeChange();    // Just for night mode hour roll-over

  // Check if LED update is needed
  for (int i=0; i<(maxObjects-1); i++) {    // Check all except the switch
    if ((currentMillis - previousObjectMillis[i]) > nextObjectInterval[i] && previousObjectMillis[i] != 0) {    // Needs update and not disabled (0)
      if (i == 2 || i == 3 || i == 8 || i == 9) {    // Filter out candle routines
        if (masterCandlesOn == 1) updateCandle(i);     
       }
       else if (i == 1 || i == 5 || i == 6) {   // Filter out firefly routines
         if (masterFireflyOn == 1) updateFirefly(i);
       }
       else switch(timeMode) {
        case 0:
          incrementSunriseColors();
          break;
        case 1:
          incrementMorningColors();
          break;
        case 2:
          incrementMiddayColors();
          break;
        case 3:
          incrementAfternoonColors();
        break;
        case 4:
          incrementSunsetColors();
          break;
        case 5:
          incrementEveningColors();
          break;
        case 6:
          incrementNightColors();
          break;
      }
    }
  }  
}

//########## Change candle flicker ##########

void updateCandle(int j) {
  
  nextObjectInterval[j] = random (houseDMin, houseDMax);
  previousObjectMillis[j] = currentMillis;
  int newValue = houseV[j] + random (houseVLow, houseVHigh);
  newValue = constrain (newValue, 1, 255);
  //Serial.print("Cycling candle ");
  //Serial.println(j);
  //Serial.print("New interval ");
  //Serial.println(nextObjectInterval[j]);
  //Serial.print("New value ");
  //Serial.println(newValue);
    
  leds[j]=CHSV(houseH,houseS,newValue);
  FastLED.show();
}

//########## Change firefly flicker ##########

void updateFirefly(int j) {
  if (fireflyV[j] == 0) {   // Turn on firefly if off
    fireflyV[j] = fireflyBrightness;
    nextObjectInterval[j] = fireflyD;
    previousObjectMillis[j] = currentMillis;
  }
  else {    // Turn off firefly
    fireflyV[j] = 0;
    nextObjectInterval[j] = random (fireflyDelayMin, fireflyDelayMax);
    previousObjectMillis[j] = currentMillis;
  }
  //Serial.print("Cycling firefly ");
  //Serial.println(j);
  //Serial.print("Delay set to: ");
  //Serial.println(nextObjectInterval[j]);
  leds[j]=CHSV(fireflyH,fireflyS,fireflyV[j]);
  FastLED.show();
}

//########## Change dayMode Colors ##########

void incrementSunriseColors() {
  //  Moon = LED 10, Sky = LED 11 - 15
  if (colorChangeStep < 100) {    // ignore if at step 100
    colorChangeStep++;
  
    int newH = map (colorChangeStep, 0, 100, moonH, riseSetHighH);
    int newS = map(colorChangeStep, 0, 100, moonS, riseSetS);
    int newV = map (colorChangeStep, 0, 100, moonV, riseSetV);
    leds[skyPosition]=CHSV(newH,newS,newV);   // Set new moon values, setting to riseSet
    
    newH = map (colorChangeStep, 0, 100, nightH, riseSetHighH);
    newS = map(colorChangeStep, 0, 100, nightS, riseSetS);
    newV = map (colorChangeStep, 0, 100, nightV, riseSetV);
    for (int i = 11; i<=15; i++) {
      leds[i]=CHSV(newH,newS,newV);   // Set new sky values toward riseSet
    }

    //Serial.print("Incrementing Sunrise colors, step ");
    //Serial.print(colorChangeStep);
    //Serial.print(" - Sky HSV(");
    //Serial.print(newH);
    //Serial.print(",");
    //Serial.print(newS);
    //Serial.print(",");
    //Serial.print(newV);
    //Serial.println(")");
 
    FastLED.show();
    previousObjectMillis[skyPosition] = currentMillis;   // Set up next sequence change

    
  }
}
void incrementMorningColors() {
  //  Sun = LED 15, Sky = LED 10 - 14, Cresting sun
  if (colorChangeStep < 100) {    // ignore if at step 100
    colorChangeStep++;
    
    //Serial.print("Incrementing Morning colors, step ");
    //Serial.println(colorChangeStep);
    
    int newH = map (colorChangeStep, 0, 100, riseSetLowH, sunH);
    int newS = map(colorChangeStep, 0, 100, riseSetS, sunS);
    int newV = map (colorChangeStep, 0, 100, riseSetV, sunV);
    leds[15]=CHSV(newH, newS, newV);   // Set new sun values

    newH = map (colorChangeStep, 0, 100, riseSetLowH, dayH);
    newS = map (colorChangeStep, 0, 100, riseSetS, dayS);
    newV = map (colorChangeStep, 0, 100, riseSetV, dayV);
    for (int i = 10; i<=14; i++) {
      leds[i]=CHSV(newH, newS, newV);   // Set new sky values away from riseSet
    }
    FastLED.show();
    previousObjectMillis[skyPosition] = currentMillis;   // Set up next sequence change
  }
}
void incrementMiddayColors() {    // Track sun across the sky
  if (skyPosition != 10) {   // If sun is in last position then skip any update here
    leds[skyPosition]=CHSV(dayH,dayS,dayV);   // Daylight background
    skyPosition--;
    leds[skyPosition]=CHSV(sunH,sunS,sunV);   // Move Sun position 1 step
    FastLED.show();

    //Serial.print("Cycling Midday Sun to position ");
    //Serial.println(skyPosition);
  
    //  Set millis for next change
    nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
    previousObjectMillis[skyPosition] = currentMillis;
    
    //nextObjectInterval[skyPosition-1] = millisPerStep;  <---- I don't think this is necessary
    //previousObjectMillis[skyPosition-1] = currentMillis;  <---- I don't think this is necessary
    
    nextObjectInterval[skyPosition+1] = 0;    // Disable previous position timing
    previousObjectMillis[skyPosition+1] = 0;
  }
}
void incrementAfternoonColors() {
  //  Sun = LED 10, Sky = LED 11 - 15
  if (colorChangeStep < 100) {    // ignore if at step 100
    colorChangeStep++;
    
    //Serial.print("Incrementing Afternoon colors, step ");
    //Serial.println(colorChangeStep);
  
    leds[skyPosition]=CHSV(map (colorChangeStep, 0, 100, sunH, riseSetLowH), map(colorChangeStep, 0, 100, sunS, riseSetS), map (colorChangeStep, 0, 100, sunV, riseSetV));   // Set new sun values
    for (int i = 11; i<=15; i++) {
      leds[i]=CHSV(map (colorChangeStep, 0, 100, dayH, riseSetLowH), map (colorChangeStep, 0, 100, dayS, riseSetS), map (colorChangeStep, 0, 100, dayV, riseSetV));   // Set new sky values toward riseSet
    }
    FastLED.show();
    previousObjectMillis[skyPosition] = currentMillis;   // Set up next sequence change
  }
}
void incrementSunsetColors() {
  //  Only sky changes = LED 10 - 15
  if (colorChangeStep < 100) {    // ignore if at step 100
    colorChangeStep++;
    
    int newH = map (colorChangeStep, 0, 100, riseSetHighH, nightH);
    int newS = map (colorChangeStep, 0, 100, riseSetS, nightS);
    int newV = map (colorChangeStep, 0, 100, riseSetV, nightV);
    
    for (int i = 10; i<=15; i++) {
      leds[i]=CHSV(newH,newS,newV );   // Set new sky values toward Night
    }

    //Serial.print("Incrementing Sunset colors, step ");
    //Serial.println(colorChangeStep);
    
    FastLED.show();
    previousObjectMillis[skyPosition] = currentMillis;   // Set up next sequence change
  }
}
void incrementEveningColors() {
  //  Moon = LED 15, Sky = LED 10 - 14
  if (colorChangeStep < 100) {    // ignore if at step 100
    colorChangeStep++;
    
    int newH = map (colorChangeStep, 0, 100, nightH, moonH);
    int newS = map(colorChangeStep, 0, 100, nightS, moonS);
    int newV = map (colorChangeStep, 0, 100, nightV, moonV);
  
    leds[skyPosition]=CHSV(newH, newS, newV);   // Set new moon values

    //Serial.print("Incrementing Evening colors, step ");
    //Serial.print(colorChangeStep);
    //Serial.print(" - Moon HSV(");
    //Serial.print(newH);
    //Serial.print(",");
    //Serial.print(newS);
    //Serial.print(",");
    //Serial.print(newV);
    //Serial.println(")");
    
    FastLED.show();
    previousObjectMillis[skyPosition] = currentMillis;   // Set up next sequence change
  }
}
void incrementNightColors() {
  if (skyPosition != 10) {   // If moon is in last position then skip any update here
    leds[skyPosition]=CHSV(nightH,nightS,nightV);   // Night background
    skyPosition--;
    leds[skyPosition]=CHSV(moonH,moonS,moonV);   // Move Moon position 1 step
    FastLED.show();

    //Serial.print("Cycling Night Moon to position ");
    //Serial.println(skyPosition);
  
    //  Set millis for next change
    nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
    previousObjectMillis[skyPosition] = currentMillis;
    //nextObjectInterval[skyPosition-1] = millisPerStep;  <-- I don't think this is necessary
    //previousObjectMillis[skyPosition-1] = currentMillis;  <-- I don't think this is necessary
    nextObjectInterval[skyPosition+1] = 0;    // Disable previous position
    previousObjectMillis[skyPosition+1] = 0;
  }
}

//########## Change time mode [timer]  ##########

void timeModeChange(){
  timeMode++;
  if (timeMode>timeModeMax)timeMode = 0;
  //Serial.print("Time mode change [timer]: ");
  //Serial.println(timeMode);

  setInitialColors();
}

//########## Change time mode [button] ##########

void forceTimeModeChange() {
  timeMode++;
  if (timeMode>timeModeMax)timeMode = 0;

  //Serial.print("Time mode change [forced]: ");
  //Serial.println(timeMode);

  setTime(timeModeInterval[timeMode],0,0,1,1,20);   // Set time/date to new mode hour, 1 Jan 2020

  previousObjectMillis[modeSwitch] = currentMillis;  // button debounce timing
  setInitialColors();
}

//########## Set Initial Colors ##########

void setInitialColors() {
  //Serial.print("Flushing timing for LED ");
  for (int i = 0; i <= 15; i++) {    // Flush LED timing
    nextObjectInterval[i] = 0;
    previousObjectMillis[i] = 0;
    //Serial.print(i);
    //Serial.print(", ");
  } //Serial.println();

  colorChangeStep = 0;    // Reset timing values
  /* Removing these, don't think they are necessary
   *  hStep[0] = 0;
   *  hStep[1] = 0;
   *  vStep[0] = 0;
   *  vStep[1] = 0;
   *  vGoal=0;
   *  Serial.println("Timing values reset (colorChangeStep, hStep[x], vStep[x], vGoal");
   */
  //Serial.println("Timing values reset: colorChangeStep");

  masterCandlesOn = 0;    // Turn off candle routine
  masterFireflyOn = 0;    // Turn off firefly routine
  //Serial.println("Candles and Firefly turned off");
  
  switch(timeMode) {
    case 0:
      setSunriseColors();
      break;
    case 1:
      setMorningColors();
      break;
    case 2:
      setMiddayColors();
      break;
    case 3:
      setAfternoonColors();
      break;
    case 4:
      setSunsetColors();
      break;
    case 5:
      setEveningColors();
      break;
    case 6:
      setNightColors();
      break;
  }
}

void setSunriseColors() {   // Transition from night blue to sunrise red + Moonset
  //Serial.println("Setting Sunrise colors");
  
  leds[7]=CHSV(0,255,nightV);    // red
  leds[0]=CHSV(random(0,255),255,nightV);
  leds[1]=CHSV(0,0,0);    // DISABLE Near bush 2(R)
  leds[2]=CHSV(0,0,0);    // DISABLE Far Door
  leds[3]=CHSV(0,0,0);    // DISABLE Far Window
  leds[4]=CHSV(random(0,255),255,nightV);
  leds[5]=CHSV(0,0,0);    // DISABLE Far bush (L)
  leds[6]=CHSV(0,0,0);    // DISABLE Near bush 1(L)
  leds[8]=CHSV(0,0,0);    // DISABLE Near door
  leds[9]=CHSV(0,0,0);    // DISABLE Near window
  leds[10]=CHSV(moonH,moonS,moonV);   // Moon position
  leds[11]=CHSV(nightH,nightS,nightV);  // Night sky
  leds[12]=CHSV(nightH,nightS,nightV);
  leds[13]=CHSV(nightH,nightS,nightV);
  leds[14]=CHSV(nightH,nightS,nightV);
  leds[15]=CHSV(nightH,nightS,nightV);
  skyPosition = 10;

  //  Calculate number of millis for next change
  millisPerStep = ((((timeModeInterval[timeMode+1] - timeModeInterval[timeMode]) * 3600000) / 100) / speedFactor);    // 100 steps for color change in 1hr
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;
  //vGoal = (dayV - nightV) / (timeModeInterval[timeMode+2] - timeModeInterval[timeMode]);  <---- I don't think this is necessary

  FastLED.show();
}


void setMorningColors() {   // Transition from sunrise red to daylight white + cresting Sun
  //Serial.println("Setting Morning colors");
  
  leds[7]=CHSV(64,255,moonV);    // yellow
  leds[0]=CHSV(random(0,255),255,moonV);
  leds[1]=CHSV(0,0,0);    // DISABLE Near bush 2(R)
  leds[2]=CHSV(0,0,0);    // DISABLE Far Door
  leds[3]=CHSV(0,0,0);    // DISABLE Far Window
  leds[4]=CHSV(random(0,255),255,moonV);
  leds[5]=CHSV(0,0,0);    // DISABLE Far bush (L)
  leds[6]=CHSV(0,0,0);    // DISABLE Near bush 1(L)
  leds[8]=CHSV(0,0,0);    // DISABLE Near door
  leds[9]=CHSV(0,0,0);    // DISABLE Near window
  leds[10]=CHSV(riseSetLowH,riseSetS,riseSetV);
  leds[11]=CHSV(riseSetLowH,riseSetS,riseSetV);
  leds[12]=CHSV(riseSetLowH,riseSetS,riseSetV);
  leds[13]=CHSV(riseSetLowH,riseSetS,riseSetV);
  leds[14]=CHSV(riseSetLowH,riseSetS,riseSetV);
  leds[15]=CHSV(riseSetLowH,riseSetS,riseSetV);
  skyPosition = 15;

  // Set random timing to turn on windows (30 - 60 minutes delay [7:30 - 8:00am]) 
  // Set random timing to turn on doors (75 - 105 minutes delay [8:15 - 8:45am])
  masterCandlesOn = 1; 
  //Serial.println("House candles random on");
  nextObjectInterval[2] = random (4500000, 6300000) / speedFactor;
  previousObjectMillis[2] = currentMillis;
  nextObjectInterval[3] = random (1800000, 3600000) / speedFactor;
  previousObjectMillis[3] = currentMillis;
  nextObjectInterval[8] = random (4500000, 6300000) / speedFactor;
  previousObjectMillis[8] = currentMillis;
  nextObjectInterval[9] = random (1800000, 3600000) / speedFactor;
  previousObjectMillis[9] = currentMillis;
  //Serial.print("Door delay: \t");
  //Serial.print(nextObjectInterval[2]);
  //Serial.print("\t");
  //Serial.println(nextObjectInterval[8]);
  //Serial.print("Window delay: \t");
  //Serial.print(nextObjectInterval[3]);
  //Serial.print("\t");
  //Serial.println(nextObjectInterval[9]);

  //  Calculate number of millis for next change
  millisPerStep = ((((timeModeInterval[timeMode+1] - timeModeInterval[timeMode]) * 3600000) / 100) / speedFactor);    // 100 steps for color change in 1hr
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;
  
  FastLED.show();
}
void setMiddayColors() {    // Daylight with tracking sun
  //Serial.println("Setting Midday colors");
  
  leds[7]=CHSV(64,50,sunV);    // white
  leds[0]=CHSV(random(0,255),255,sunV);
  leds[1]=CHSV(0,0,0);    // DISABLE Near bush 2(R)
  leds[2]=CHSV(0,0,0);    // DISABLE Far Door
  leds[3]=CHSV(0,0,0);    // DISABLE Far Window
  leds[4]=CHSV(random(0,255),255,sunV);
  leds[5]=CHSV(0,0,0);    // DISABLE Far bush (L)
  leds[6]=CHSV(0,0,0);    // DISABLE Near bush 1(L)
  leds[8]=CHSV(0,0,0);    // DISABLE Near door
  leds[9]=CHSV(0,0,0);    // DISABLE Near window
  leds[10]=CHSV(dayH,dayS,dayV);   // Sunlight background
  leds[11]=CHSV(dayH,dayS,dayV);
  leds[12]=CHSV(dayH,dayS,dayV);
  leds[13]=CHSV(dayH,dayS,dayV);
  leds[14]=CHSV(dayH,dayS,dayV);
  leds[15]=CHSV(sunH,sunS,sunV);   // Sun position rear
  skyPosition=15;
  
  //  Calculate number of millis for next change
  millisPerStep = ((((timeModeInterval[timeMode+1] - timeModeInterval[timeMode]) * 3600000) / 6) / speedFactor);
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;
  nextObjectInterval[skyPosition-1] = millisPerStep;
  previousObjectMillis[skyPosition-1] = currentMillis;
   
  FastLED.show();
}
void setAfternoonColors() {   // Transition daylight to sunset red + Sundown
  //Serial.println("Setting Afternoon colors");
  
  leds[7]=CHSV(32,255,moonV);    // orange
  leds[0]=CHSV(random(0,255),255,moonV);
  leds[1]=CHSV(0,0,0);    // DISABLE Near bush 2(R)
  leds[2]=CHSV(houseH,houseS,0);
  leds[3]=CHSV(0,0,0);    // DISABLE Far Window
  leds[4]=CHSV(random(0,255),255,moonV);
  leds[5]=CHSV(0,0,0);    // DISABLE Far bush (L)
  leds[6]=CHSV(0,0,0);    // DISABLE Near bush 1(L)
  leds[8]=CHSV(houseH,houseS,0);
  leds[9]=CHSV(0,0,0);    // DISABLE Near window
  leds[10]=CHSV(sunH,sunS,sunV);   // Sun position
  leds[11]=CHSV(dayH,dayS,dayV);   // Sunlight background
  leds[12]=CHSV(dayH,dayS,dayV);
  leds[13]=CHSV(dayH,dayS,dayV);
  leds[14]=CHSV(dayH,dayS,dayV);
  leds[15]=CHSV(dayH,dayS,dayV); 
  skyPosition = 10;

  // Turn on doors and set random timing (up to 30 minutes delay [5:00 - 5:30pm])
  masterCandlesOn = 1; 
  //Serial.println("Door candles on");   
  nextObjectInterval[2] = random (0, 1800000) / speedFactor;
  previousObjectMillis[2] = currentMillis;
  nextObjectInterval[8] = random (0, 1800000) / speedFactor;
  previousObjectMillis[8] = currentMillis;
  //Serial.print("Door delay: \t");
  //Serial.print(nextObjectInterval[2]);
  //Serial.print("\t");
  //Serial.println(nextObjectInterval[8]);
  
  FastLED.show();

  //  Calculate number of millis for next change
  millisPerStep = ((((timeModeInterval[timeMode+1] - timeModeInterval[timeMode]) * 3600000) / 100) / speedFactor);    // 100 steps for color change in 1hr
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;
}

void setSunsetColors() {    // Transition sunset red to night blue
  //Serial.println("Setting Sunset colors");
  
  leds[7]=CHSV(224,255,moonV);    // pink
  leds[0]=CHSV(random(0,255),255,moonV);
  leds[1]=CHSV(0,0,0);    // DISABLE Near bush 2(R)
  leds[2]=CHSV(houseH,houseS,houseV[2]);
  leds[3]=CHSV(houseH,houseS,0);
  leds[4]=CHSV(random(0,255),255,moonV);
  leds[5]=CHSV(0,0,0);    // DISABLE Far bush (L)
  leds[6]=CHSV(0,0,0);    // DISABLE Near bush 1(L)
  leds[8]=CHSV(houseH,houseS,houseV[8]);
  leds[9]=CHSV(houseH,houseS,0);
  leds[10]=CHSV(riseSetHighH,riseSetS,riseSetV);
  leds[11]=CHSV(riseSetHighH,riseSetS,riseSetV);
  leds[12]=CHSV(riseSetHighH,riseSetS,riseSetV);
  leds[13]=CHSV(riseSetHighH,riseSetS,riseSetV);
  leds[14]=CHSV(riseSetHighH,riseSetS,riseSetV);
  leds[15]=CHSV(riseSetHighH,riseSetS,riseSetV);

  // Turn on doors and set random timing to turn on windows (30 - 60 minutes delay [7:30 - 8:00pm]) 
  masterCandlesOn = 1; 
  //Serial.println("Door candles on");
  updateCandle(2);
  nextObjectInterval[3] = random (1800000, 3600000) / speedFactor;
  previousObjectMillis[3] = currentMillis;
  updateCandle(8);
  nextObjectInterval[9] = random (1800000, 3600000) / speedFactor;
  previousObjectMillis[9] = currentMillis;
  //Serial.print("Window delay: \t");
  //Serial.print(nextObjectInterval[3]);
  //Serial.print("\t");
  //Serial.println(nextObjectInterval[9]);

  skyPosition = 10;

  //  Calculate number of millis for next change
  millisPerStep = ((((timeModeInterval[timeMode+1] - timeModeInterval[timeMode]) * 3600000) / 100) / speedFactor);    // 100 steps for color change in 1hr
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;

  FastLED.show();
}

void setEveningColors() {   // Night blue with rising moon.  Moon rises in 1 hr, no change for 2nd hour
  //Serial.println("Setting Evening colors");
  
  leds[7]=CHSV(160,255,50);    // blue
  leds[0]=CHSV(random(0,255),255,50);
  leds[1]=CHSV(fireflyH,fireflyS,0);    // Near bush 2(R)
  leds[2]=CHSV(0,0,0);    // DISABLE Far Door
  leds[3]=CHSV(houseH,houseS,houseV[3]);
  leds[4]=CHSV(random(0,255),255,50);
  leds[5]=CHSV(fireflyH,fireflyS,0);    // Far bush (L)
  leds[6]=CHSV(fireflyH,fireflyS,0);    // Near bush 1(L)
  leds[8]=CHSV(0,0,0);    // DISABLE Near door
  leds[9]=CHSV(houseH,houseS,houseV[9]);
  leds[10]=CHSV(nightH,nightS,nightV);   // Night sky
  leds[11]=CHSV(nightH,nightS,nightV);
  leds[12]=CHSV(nightH,nightS,nightV);
  leds[13]=CHSV(nightH,nightS,nightV);
  leds[14]=CHSV(nightH,nightS,nightV);
  leds[15]=CHSV(nightH,nightS,nightV);
  skyPosition=15;
  
  // Turn on window Candles, doors are off
  masterCandlesOn = 1; 
  //Serial.println("window Candles on");   
  updateCandle(3);
  updateCandle(9);
  
  // Turn on Firefly and set random start time
  masterFireflyOn = 1;    // Turn on fireflys
  previousObjectMillis[1] = currentMillis;
  nextObjectInterval[1] = random (fireflyDelayMin, fireflyDelayMax);
  previousObjectMillis[5] = currentMillis;
  nextObjectInterval[5] = random (fireflyDelayMin, fireflyDelayMax);
  previousObjectMillis[6] = currentMillis;
  nextObjectInterval[6] = random (fireflyDelayMin, fireflyDelayMax);
  //Serial.print("Firefly delays \t");
  //Serial.print(nextObjectInterval[1]);
  //Serial.print("\t");
  //Serial.print(nextObjectInterval[5]);
  //Serial.print("\t");
  //Serial.println(nextObjectInterval[6]);

  //  Calculate number of millis for next change
  millisPerStep = ((((timeModeInterval[timeMode+1] - timeModeInterval[timeMode]) * 3600000) / 100) / speedFactor);    // 100 steps for color change in 1hr
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;
  
  FastLED.show();
}

void setNightColors() {   // Blue sky with moon tracking
  //Serial.println("Setting Night colors");
  
  leds[7]=CHSV(192,255,40);    // purple
  leds[0]=CHSV(random(0,255),255,40);
  leds[1]=CHSV(0,0,0);    // DISABLE Near bush 2(R)
  leds[2]=CHSV(0,0,0);    // DISABLE Far Door
  leds[3]=CHSV(0,0,0);    // DISABLE Far Window
  leds[4]=CHSV(random(0,255),255,40);
  leds[5]=CHSV(0,0,0);    // DISABLE Far bush (L)
  leds[6]=CHSV(0,0,0);    // DISABLE Near bush 1(L)
  leds[8]=CHSV(0,0,0);    // DISABLE Near door
  leds[9]=CHSV(0,0,0);    // DISABLE Near window
  leds[10]=CHSV(nightH,nightS,nightV);    // Night sky
  leds[11]=CHSV(nightH,nightS,nightV);
  leds[12]=CHSV(nightH,nightS,nightV);
  leds[13]=CHSV(nightH,nightS,nightV);
  leds[14]=CHSV(nightH,nightS,nightV);
  leds[15]=CHSV(moonH,moonS,moonV);   // Moon position
  skyPosition=15;
  //  Calculate number of millis for next change
  millisPerStep = (((((timeModeInterval[0] + 24) - timeModeInterval[timeMode]) * 3600000) / 6) / speedFactor);
  //Serial.print("millisPerStep \t");
  //Serial.println(millisPerStep);
  
  nextObjectInterval[skyPosition] = millisPerStep;   // Set up next sequence change
  previousObjectMillis[skyPosition] = currentMillis;
  nextObjectInterval[skyPosition-1] = millisPerStep;
  previousObjectMillis[skyPosition-1] = currentMillis;
  
  FastLED.show();
}

//########## Testing scripts ##########

void showVariables (){            // Show all variables for diagnostic purposes
  
  //Serial.println("----------------------------------------------------------------");

  //Serial.print("currentMillis ");
  //Serial.println(currentMillis);

  //Serial.print("millisPerStep ");
  //Serial.println(millisPerStep);

  //Serial.print("nextObjectInterval[skyPosition] ");
  //Serial.println(nextObjectInterval[skyPosition]);

  //Serial.print("hour ");
  //Serial.println(hour());

  //Serial.print("currentHour ");
  //Serial.println(currentHour);
  
  //Serial.print("previousObjectMillis");
  //Serial.print("\t");
  //for (int i=0; i<maxObjects; i++) {
  //  Serial.print(previousObjectMillis[i]);
  //  Serial.print("\t");
  //}
  //Serial.println();
  
  //Serial.print("nextObjectInterval");
  //Serial.print("\t");
  //for (int i=0; i<maxObjects; i++) {
  //  Serial.print(nextObjectInterval[i]);
  //  Serial.print("\t");
  //}
  //Serial.println();

  //Serial.print("timeMode");
  //Serial.print("\t");
  //Serial.println(timeMode);

  /*
   * Serial.print("timeModeInterval");
   * Serial.print("\t");
   * for (int i=0; i<=timeModeMax; i++) {
   *   Serial.print(timeModeInterval[i]);
   *   Serial.print("\t");
   * }
   * Serial.println();
   */

  //Serial.print("skyPosition");
  //Serial.print("\t");
  //Serial.println(skyPosition);
  
}
