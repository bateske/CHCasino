// CHSDtoUSB's simulator: chsim's Arduino stand-in plus the CHGame pins and
// core functions this sketch uses.
#pragma once
#include_next <Arduino.h>

enum : uint32_t {
    PIN_BTN_UP = 40, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
    PIN_BTN_A, PIN_BTN_B, PIN_BTN_SELECT, PIN_BTN_START, PIN_SD_CS,
};
#define SS 4
#define MOSI 7
#define MISO 6
#define SCK 5
#define PIN_SPI_SS SS
#define PIN_SPI_MOSI MOSI
#define PIN_SPI_MISO MISO
#define PIN_SPI_SCK SCK
void NVIC_SystemReset();
