#include "Arduino.h"
//
#include <Adafruit_GFX.h>      // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library

class Card {
public:
	enum Number {
		NumAce   = 1,
		Num2     = 2,
		Num3     = 3,
		Num4     = 4,
		Num5     = 5,
		Num6     = 6,
		Num7     = 7,
		Num8     = 8,
		Num9     = 9,
		Num10    = 10,
		NumJack  = 11,
		NumQueen = 12,
		NumKing  = 13,
	};
	enum Suit {
		Spades   = 0,
		Hearts   = 1,
		Diamonds = 2,
		Clubs    = 3,
	};

public:
	Card(Number n, Suit s): mNumber(n), mSuit(s) {}

	Number getNumber() const {return (Number)mNumber; }
	Suit getSuit() const {return (Suit)mSuit; }

private:
	struct {
		unsigned mNumber: 4;
		unsigned mSuit:   4;
	};
};


///////////////////////////////////////////////////////////////////////////////
// standard U of A library settings, assuming Atmel Mega SPI pins
#define SD_CS      5  // Chip select line for SD card
#define TFT_CS     6  // Chip select line for TFT display
#define TFT_DC     7  // Data/command line for TFT
#define TFT_RST    8  // Reset line for TFT (or connect to +5V)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);



///////////////////////////////////////////////////////////////////////////////
void DrawCard(const Card& c, int atx, int aty) {
	tft.drawRect(atx, aty, 14, 20, tft.Color565(255,255,255));
}



///////////////////////////////////////////////////////////////////////////////
void setup() {
	//std::cout << sizeof(Card) << "\n";
	Serial.begin(9600);
	tft.initR(INITR_REDTAB);   // initialize a ST7735R chip, red tab

	tft.fillScreen(tft.Color565(0,0,0));
	for (int i = 0; i < 7; ++i) {
		DrawCard(Card(Card::Num4, Card::Spades), 2 + 16*i, 25);
	}
}

void loop() {}












