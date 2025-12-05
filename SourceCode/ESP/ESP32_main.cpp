#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Audio.h>

#define SD_CS     18
#define SPI_SCK   36
#define SPI_MISO  37
#define SPI_MOSI  35

// I2S (to MAX98357A)
#define I2S_BCLK  11
#define I2S_LRCLK 10
#define I2S_DOUT   7

// UART to ATmega
#define UART_RX_PIN  6
#define UART_TX_PIN  5
#define UART_BAUD    115200

Audio audio;

bool playing = false; 

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("FeatherS2: SD + I2S + UART to ATmega");

  // UART1 for ATmega
  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  // SD Card setup
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  if (!SD.begin(SD_CS)) {
    Serial.println("Error: SD card init failed!");
    Serial1.write('a');
    while (true) {
      Serial.println("Error: SD card init failed!");
      delay(1000);
    }
  }
  Serial.println("SD card OK");

  // MAX98357A setup
  audio.setPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
  audio.setVolume(90);

  Serial.println("Ready. Waiting for 'a' to start, 'b' to stop.");
}

void loop() {
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    Serial.print("ATmega: ");
    Serial.println(c);

    if (c == 'a') {
      if (!playing) {
        Serial.println("Command: START music");
        audio.connecttoFS(SD, "/music.mp3"); 
        playing = true;
      }
    } else if (c == 'b') {
      if (playing) {
        Serial.println("Command: STOP music");
        audio.stopSong();  
        playing = false;
      }
    }
  }

  if (playing) {
    audio.loop();
  }
}
