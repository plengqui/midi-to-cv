#include <MIDI.h>
#include <midi_Defs.h>
#include <midi_Message.h>
#include <midi_Namespace.h>
#include <midi_Settings.h>
#include <FreqMeasure.h>
#include <math.h>

/*
https://github.com/petegaggs/MIDI-to-CV/blob/master/midi_cv2.ino
  midi_cv
  MIDI to CV converter
  control voltage provided by MCP4821 SPI DAC  
  uses Arduino MIDI library
  was origninally developed for a monotron, this version is general purpose
  */

// inslude the SPI library:
#include <SPI.h>
//include the midi library:
MIDI_CREATE_DEFAULT_INSTANCE();

//low power stuff
//#include <avr/power.h>
//#include <avr/sleep.h>

//MIDI_CREATE_DEFAULT_INSTANCE();
#define GATE_PIN 9 //gate control
#define SLAVE_SELECT_PIN 10 //spi chip select

#define GATE_RETRIGGER_DELAY_US 100 //time in microseconds to turn gate off in order to retrigger envelope

//MIDI variables
int currentMidiNote; //the note currently being played
int keysPressedArray[128] = {0}; //to keep track of which keys are pressed

void setup() {
  FreqMeasure.begin(); //input on pin 8 for arduino nano
  //MIDI stuff
  // Initiate MIDI communications, listen to all channels
  MIDI.begin(MIDI_CHANNEL_OMNI);      
  // Connect the HandleNoteOn function to the library, so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(HandleNoteOn);  // Put only the name of the function
  // Do the same for NoteOffs
  MIDI.setHandleNoteOff(handleNoteOff);
  //SPI stuff
  // set the slaveSelectPin as an output:
  pinMode (SLAVE_SELECT_PIN, OUTPUT);
  digitalWrite(SLAVE_SELECT_PIN,HIGH); //set chip select high
  // initialize SPI:
  SPI.begin(); 
  Serial.begin(57600); //for debug, can't use midi at the same time!
  pinMode (GATE_PIN, OUTPUT); //gate for monotron
  digitalWrite(GATE_PIN,LOW); //turn note off
  //dacWrite(0); //set the pitch just for testing
  Serial.println("calibration1");
  calibrate();

}

float calibrated_voltages_for_notes[128]; //calibration fills this array with correct cv voltages in millivolt per midi note (index of array)

void calibrate(){
  const int cal_test_notes[] = {36,48,60,72,84}; // midi notes
  const int cal_len = 5;
  float cal_measured_fq[cal_len]; 
  float cal_millivolts[cal_len];
  int DAC_BASE = -3000; //-3V offset 
  float DAC_MILLIVOLT_PER_SEMITONE = 1000/12; // 1 volt per octave -> One semitone is 1/12th of a volt
  dacWrite(DAC_BASE+0*DAC_MILLIVOLT_PER_SEMITONE); //set the pitch of the oscillator
  FreqMeasure.read(); //i guess this "resets" the frequency measurements, not sure...
  Serial.println("Connect for calibration");
  while(!FreqMeasure.available()){} //wait for note on pin 8
  Serial.println("Detected tone, starting calibration");
  for (int i = 0; i < cal_len; i++) { //loop over cal_notes but omit first and last one
    cal_millivolts[i] = DAC_BASE + cal_test_notes[i]*DAC_MILLIVOLT_PER_SEMITONE;
    FreqMeasure.read(); //i guess this "resets" the frequency measurements, not sure...
    dacWrite(cal_millivolts[i]); //set the pitch of the oscillator
    delay(100);//allow dac to settle (not sure if needed but i can spare the time)
    while(FreqMeasure.available())
      FreqMeasure.read(); //empty the buffer
    float fq = 440 * pow(2,((float)cal_test_notes[i]- 69)/12);
    Serial.print("Expect ");
    Serial.print(fq);
    double sum=0;
    int count=0;
    for(int j =0; j<100;j++){ 
      delay(5);
      if (FreqMeasure.available()) {
        // average several reading together
        double r = FreqMeasure.read();
        //Serial.print(FreqMeasure.countToFrequency(r));
        //Serial.print(" ");
        sum = sum + r;
        count = count + 1;
      }
    }
    float frequency = FreqMeasure.countToFrequency(sum / count);
    Serial.print(" got ");
    Serial.print(frequency);
    Serial.println(" Hz");
    cal_measured_fq[i]=frequency; 
    sum = 0;
    count = 0;
  }
  Serial.println("");
  
  //create array with midi note values for the calibrations
  float cal_notes[cal_len];
  for (int i = 0; i < cal_len; i++) { 
    cal_notes[i] = (log(cal_measured_fq[i]) - log(440))*12/log(2) + 69;
  }

  //calculate voltages for all midi notes
  for(int note = 0; note<128; note++){
    if(cal_notes[cal_len-1] < note){
      calibrated_voltages_for_notes[note] = cal_millivolts[cal_len-1];
      continue;
    }
    int interval = 0;
    for(interval = 1; cal_notes[interval] < note && interval < cal_len; interval++){}
    //detect if note is higher than lowest or vice versa
    float x1 = cal_millivolts[interval-1];
    float x2 = cal_millivolts[interval];
    float y1 = cal_notes[interval-1];
    float y2 = cal_notes[interval];
    calibrated_voltages_for_notes[note] = x2 - (x2-x1)*(y2 - note)/(y2-y1);
  }
  
  for(int note = 36; note<48; note++){
    for(int i =0;i<3;i++){
      dacWrite(calibrated_voltages_for_notes[note + i*12]); 
      delay(20);
      Serial.println(calibrated_voltages_for_notes[note + i*12]-calibrated_voltages_for_notes[note + i*12 - 1]);
    }
  }

}



void old_loop(){
  //if (FreqMeasure.available()) {    Serial.println(FreqMeasure.countToFrequency(FreqMeasure.read()) );}
}

void loop() {
  const int d=50;
  const int c=d-2;
  const int b=c-3;
  const int a=b-2;
  // d b c d c b a b
  const int seq[] = {d, b, c, d, c, b, a, b};   int note_duration = 120; //billie_jean
  int len = sizeof(seq)/sizeof(a);
  for (int i = 0; i < len; i++) {
    HandleNoteOn(1, seq[i], 127);
    delay(note_duration); 
    handleNoteOff(1, seq[i], 127); 
    delay(note_duration); 
  }
  //MIDI.read();
}

void dacWrite(int value) {
  //write a 12 bit number to the MCP8421 DAC
  if ((value < 0) || (value > 4095)) {
    value = 0;
  }
  // take the SS pin low to select the chip:
  digitalWrite(SLAVE_SELECT_PIN,LOW);
  //send a value to the DAC
  SPI.transfer(0x10 | ((value >> 8) & 0x0F)); //bits 0..3 are bits 8..11 of 12 bit value, bits 4..7 are control data 
  SPI.transfer(value & 0xFF); //bits 0..7 of 12 bit value
  // take the SS pin high to de-select the chip:
  digitalWrite(SLAVE_SELECT_PIN,HIGH); 
}


void setNotePitch(int note) {
  //receive a midi note number and set the DAC voltage accordingly for the pitch CV
  dacWrite(calibrated_voltages_for_notes[note]); //set the pitch of the oscillator
}


void HandleNoteOn(byte channel, byte pitch, byte velocity) { 
  //Serial.print("HandleNoteOn, pitch=");  Serial.print(pitch);  Serial.println("");
  // this function is called automatically when a note on message is received 
  keysPressedArray[pitch] = 1;
  synthNoteOn(pitch);
}

void handleNoteOff(byte channel, byte pitch, byte velocity){
  //Serial.print("HandleNoteOff, pitch=");  Serial.print(pitch);  Serial.println("");
  keysPressedArray[pitch] = 0; //update the array holding the keys pressed 
  if (pitch == currentMidiNote) {
    //only act if the note released is the one currently playing, otherwise ignore it
    int highestKeyPressed = findHighestKeyPressed(); //search the array to find the highest key pressed, will return -1 if no keys pressed
    if (highestKeyPressed != -1) { 
      //there is another key pressed somewhere, so the note off becomes a note on for the highest note pressed
      synthNoteOn(highestKeyPressed);
    }    
    else  {
      //there are no other keys pressed so proper note off
      synthNoteOff();
    }
  }  
}

int findHighestKeyPressed(void) {
  //Serial.println("findHighestKeyPressed");
  //search the array to find the highest key pressed. Return -1 if no keys are pressed
  int highestKeyPressed = -1; 
  for (int count = 0; count < 127; count++) {
    //go through the array holding the keys pressed to find which is the highest (highest note has priority), and to find out if no keys are pressed
    if (keysPressedArray[count] == 1) {
      highestKeyPressed = count; //find the highest one
    }
  }
  return(highestKeyPressed);
}

void synthNoteOn(int note) {
  //Serial.println("synthNoteOn");
  //starts playback of a note
  setNotePitch(note); //set the oscillator pitch
  digitalWrite(GATE_PIN,LOW); //turn gate off momentarily to retrigger LFO
  delayMicroseconds(GATE_RETRIGGER_DELAY_US); //should not do delays here really but get away with this which seems to be the minimum a montotron needs (may be different for other synths)
  digitalWrite(GATE_PIN,HIGH); //turn gate on
  currentMidiNote = note; //store the current note
}

void synthNoteOff(void) {
  digitalWrite(GATE_PIN,LOW); //turn gate off
}
