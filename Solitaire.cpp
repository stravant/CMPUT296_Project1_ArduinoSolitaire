//
#include "Arduino.h"
//
#include <Adafruit_GFX.h>      // Core graphics library
#include "Mod_Adafruit_ST7735.h" // Hardware-specific library


///////////////////////////////////////////////////////////////////////////////
// utilities

int smartmod(int a, int b) {
	return (abs(a * b) + a) % b;
}


///////////////////////////////////////////////////////////////////////////////
// standard U of A library settings, assuming Atmel Mega SPI pins
#define SD_CS      5  // Chip select line for SD card
#define TFT_CS     6  // Chip select line for TFT display
#define TFT_DC     7  // Data/command line for TFT
#define TFT_RST    8  // Reset line for TFT (or connect to +5V)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

void error(const char* c) {
	tft.fillScreen(ST7735_BLUE);
	tft.setRotation(0);
	tft.setTextWrap(true);
	tft.setTextColor(ST7735_WHITE, ST7735_BLUE);
	tft.println("0x77FF4588 STOP:\n00567094 02345778\n");
	tft.println(c);
	while (true);
}



///////////////////////////////////////////////////////////////////////////////
// 
class CardId {
public:
	enum Number {
		NumZone  = 0,
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
		Hearts   = 0,
		Spades   = 1,
		Diamonds = 2,
		Clubs    = 3,
	};

public:
	CardId(Number n, Suit s): mNumber(n), mSuit(s) {}

	static CardId RandomCard() {
		return CardId((Number)(rand() % 13 + 1), (Suit)(rand() % 4));
	}

	Number getNumber() const {return (Number)mNumber; }
	Suit getSuit() const {return (Suit)mSuit; }
	bool getColor() const {return mSuit & 0x1;} //black => true, red => false

	char getSymbol() const {
		switch (mNumber) {
		case NumAce:   return 'A';
		case Num2:     return '2';
		case Num3:     return '3';
		case Num4:     return '4';
		case Num5:     return '5';
		case Num6:     return '6';
		case Num7:     return '7';
		case Num8:     return '8';
		case Num9:     return '9';
		case Num10:    return '0';
		case NumJack:  return 'J';
		case NumQueen: return 'Q';
		case NumKing:  return 'K';
		default:       return '!';
		}
	}

	char getSuitSymbol() const {
		switch (mSuit) {
		case Hearts:   return 0x03;
		case Diamonds: return 0x04;
		case Clubs:    return 0x05;
		case Spades:   return 0x06;
		}
	}

	//convert between ints [0,51] and CardIds
	int8_t tohash() const {
		return ((int)mSuit)*13 + (((int)mNumber) - 1);
	}
	static CardId fromhash(int8_t hash) {
		Number n = static_cast<Number>((hash%13)+1);
		Suit s = static_cast<Suit>((int)(hash/13));
		return CardId(n, s);
	}

private:
	struct {
		unsigned mNumber: 4;
		unsigned mSuit:   4;
	};
};



///////////////////////////////////////////////////////////////////////////////
// 
struct Rect {
	int X, Y, W, H;
	void zero() {
		X = 0; Y = 0; W = 0; H = 0;
	}
	void expand(const Rect& other) {
		if (other.W == 0 || other.H == 0) return;
		int left = max(X+W, other.X+other.W);
		int bottom = max(Y+H, other.Y+other.H);
		X = min(X, other.X);
		Y = min(Y, other.Y);
		W = left-X;
		H = bottom-Y;
	}
	bool intersects(const Rect& other) {
		if (W == 0 || H == 0 || other.W == 0 || other.H == 0) return false;
		if (X > other.X + other.W || other.X > X + W) return false;
		if (Y > other.Y + other.H || other.Y > Y + H) return false;
		return true;
	}
};

class Card {
public:
	enum Location {
		LocationDeck,
		LocationStack,
		LocationBoard,
		LocationDrag,
	};

public:
	Card(): Next(0), Prev(0), FaceUp(true), Highlight(0), Location(LocationDeck), 
		Which(CardId(CardId::NumZone, CardId::Hearts)) {
		LastDrawnAt.zero();
	}
	Card(CardId which): Next(0), Prev(0), FaceUp(true), Highlight(0), 
		Location(LocationDeck), Which(which) {
		LastDrawnAt.zero();
	}
	//
	bool isempty() const {return Which.getNumber() == CardId::NumZone; }
	//
	Rect LastDrawnAt;
	//
	Card* Next;
	Card* Prev;
	bool FaceUp;
	int Highlight; //is this card highlighted 0 => not, 1 => selection, 2 => target
	Location Location;
	const CardId Which;
};



///////////////////////////////////////////////////////////////////////////////
// 
class Deck {
public:
	Deck() {
		//call constructors using placement new
		for (int i = 0; i < 52; ++i)
			mDeck[i] = new Card(CardId::fromhash(i)); 
	}
	~Deck() {
		//free the cards
		for (int i = 0; i < 52; ++i)
			delete mDeck[i];
	}

	void shuffle() {
		//Fisher-Yates shuffle the deck
		for (int i = 0; i < 52; ++i) {
			int j = i + rand()%(52-i);
			Card* tmp = mDeck[j];
			mDeck[j] = mDeck[i];
			mDeck[i] = tmp;
		}
	}

	Card* operator[](int8_t i) const {
		return mDeck[i];
	}

private:
	Card* mDeck[52];
};


///////////////////////////////////////////////////////////////////////////////
// 
class BoardState {
public:
	BoardState(): mDeck(0), mTopOfDeck(0) {
		memset(mStacks, 0, 4*sizeof(Card*));
		memset(mBoard, 0, 7*sizeof(Card*));
		mSelectedColor = tft.Color565(220, 0, 140);
		mGrabColor = tft.Color565(140, 0, 220);
	}
	~BoardState() {}

	void initialize() {
		//seed the randomness for the shuffle
		for (int i = 0; i < 10; ++i)
			srand(analogRead(7) + rand());
		//
		mHasHeldCards = false;
		//start out the cursor in the right place
		mCursorLocationX = 1;
		mCursorLocationY = 0;
		//shuffle the deck
		mSourceDeck.shuffle();
		//create an initial dirty region over the whole screen
		mDirtyRegion.X = 0;
		mDirtyRegion.Y = 0;
		mDirtyRegion.W = 160;
		mDirtyRegion.H = 128;
		//
		int8_t curCard = 0;

		//deal out the piles
		for (int pileN = 0; pileN < 7; ++pileN) {
			Card* prev = &mBoardBases[pileN];
			Card* first = prev;
			prev->Prev = 0;
			prev->Location = Card::LocationBoard;
			prev->FaceUp = false;
			//
			for (int i = 0; i <= pileN; ++i) {
				Card* cur = mSourceDeck[curCard++];
				prev->Next = cur;
				cur->Prev = prev;
				cur->Location = Card::LocationBoard;
				cur->FaceUp = false;
				prev = cur;
			}
			prev->FaceUp = true; //last card is face up
			prev->Next = 0;
			mBoard[pileN] = first;
		}

		//init the stacks
		for (int stackN = 0; stackN < 4; ++stackN) {
			mStacks[stackN] = &mStackBases[stackN];
			mStacks[stackN]->Next = 0;
			mStacks[stackN]->Prev = 0;
		} 

		//link up the deck
		Card* prev = mSourceDeck[curCard++];
		Card* first = prev;
		prev->Prev = 0;
		prev->Location = Card::LocationDeck;
		while (curCard < 52) {
			Card* cur = mSourceDeck[curCard++];
			cur->Location = Card::LocationDeck;
			cur->Prev = prev;
			prev->Next = cur;
			prev = cur;
		}
		prev->Next = 0;

		//deck
		mDeck = first;
		mTopOfDeck = 0; // 0 => there are no cards currently veiwed
	}

	void flip3() {
		if (mTopOfDeck) {
			if (mTopOfDeck->Next) {
				//we have a top of deck, and a next, flip over the next cards
				for (int i = 0; i < 3; ++i) {
					if (mTopOfDeck->Next)
						mTopOfDeck = mTopOfDeck->Next;
				}
			} else {
				//no next, flip the stack back into the deck
				mTopOfDeck = 0;
			}
		} else if (mDeck) {
			//no top of deck, but a non-empty deck, deal from it
			mTopOfDeck = mDeck;
			for (int i = 0; i < 2; ++i) {
				if (mTopOfDeck->Next)
					mTopOfDeck = mTopOfDeck->Next;
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////
	// drawing code
	void drawCard(const CardId& c, int atx, int aty, Card* cardHint, bool drawSmall = false) {
		uint8_t drawSmallMod = drawSmall ? 12 : 0; 
		//where we should draw
		if (cardHint) {
			cardHint->LastDrawnAt.X = atx;
			cardHint->LastDrawnAt.Y = aty;
			cardHint->LastDrawnAt.W = 21;
			cardHint->LastDrawnAt.H = 28 - drawSmallMod;
			if (!cardHint->LastDrawnAt.intersects(mDirtyRegion)) return;
		}
		//
		static uint16_t borderColor = tft.Color565(200,200,200);
		static uint16_t borderDarkerColor = tft.Color565(100,100,100);
		tft.fillRect(atx, aty, 20, 26 - drawSmallMod, ST7735_WHITE);
		tft.drawRect(atx, aty, 20, 26 - drawSmallMod, borderColor);
		tft.drawFastHLine(atx+16, aty, 4, borderDarkerColor);
		tft.drawFastVLine(atx+20, aty, 26 - drawSmallMod, borderDarkerColor);
		//
		int16_t cardColor = c.getColor() ? ST7735_BLACK : ST7735_RED;
		//
		char symb = c.getSymbol();
		int8_t suitoffset = 0;
		if (symb == '0') {
			//special handling for 10
			suitoffset = 11;
			tft.drawChar(atx+0, aty+1, '1', cardColor, ST7735_WHITE, 1);
			tft.drawChar(atx+5, aty+1, '0', cardColor, ST7735_WHITE, 1);
		} else {
			suitoffset = 7;
			tft.drawChar(atx+1, aty+1, symb, cardColor, ST7735_WHITE, 1);
		}
		//
		tft.drawChar(atx+suitoffset, aty+1, c.getSuitSymbol(), cardColor, ST7735_WHITE, 1);
		if (cardHint)
			mDirtyRegion.expand(cardHint->LastDrawnAt);
	}
	void drawCardBack(int atx, int aty, Card* cardHint, bool drawSmall = false) {
		uint8_t drawSmallMod = drawSmall ? 12 : 0; 
		//where we would draw
		if (cardHint) {
			cardHint->LastDrawnAt.X = atx;
			cardHint->LastDrawnAt.Y = aty;
			cardHint->LastDrawnAt.W = 21;
			cardHint->LastDrawnAt.H = 28 - drawSmallMod;
			if (!cardHint->LastDrawnAt.intersects(mDirtyRegion)) return;
		}
		static uint16_t borderColor = tft.Color565(200,200,200);
		static uint16_t borderDarkerColor = tft.Color565(100,100,100);
		static uint16_t backBlue = tft.Color565(0, 50, 255);
		tft.fillRect(atx, aty, 20, 26 - drawSmallMod, ST7735_WHITE);
		tft.fillRect(atx+2, aty+2, 16, 22 - drawSmallMod + (drawSmall ? 3 : 0), backBlue);
		tft.drawRect(atx, aty, 20, 26 - drawSmallMod, borderColor);
		tft.drawFastHLine(atx+16, aty, 4, borderDarkerColor);
		tft.drawFastVLine(atx+20, aty, 26 - drawSmallMod, borderDarkerColor);
		if (cardHint)
			mDirtyRegion.expand(cardHint->LastDrawnAt);
	}

	void drawCursor(uint8_t x, uint8_t y) {
		Rect r; r.X = x, r.Y = y; r.W = 21; r.H = 28; 
		mDirtyRegion.expand(r);
		tft.drawRect(x, y, 20, 26, mSelectedColor);
		tft.drawFastVLine(x+1, y+7, 17, mSelectedColor);
		tft.drawFastVLine(x+18, y+1, 24, mSelectedColor);
	}
	void drawGrabCursor(uint8_t x, uint8_t y) {
		Rect r; r.X = x, r.Y = y; r.W = 21; r.H = 28; 
		mDirtyRegion.expand(r);
		tft.drawRect(x, y, 20, 26, mGrabColor);
		tft.drawFastVLine(x+1, y+7, 17, mGrabColor);
		tft.drawFastVLine(x+18, y+1, 24, mGrabColor);
	}
	void invalidateDeckRegion() {
		Rect r; r.X = 0; r.Y = 0; r.W = 75; r.H = 14;
		mDirtyRegion.expand(r);
	}

	void draw() {
		//clamp the dirty region to the sceen size
		if (mDirtyRegion.X < 0) mDirtyRegion.X = 0;
		if (mDirtyRegion.Y < 0) mDirtyRegion.Y = 0;
		if (mDirtyRegion.X + mDirtyRegion.W >` 160) mDirtyRegion.W = 160 - mDirtyRegion.X;
		if (mDirtyRegion.Y + mDirtyRegion.H > 128) mDirtyRegion.H = 128 - mDirtyRegion.Y;

		//background
		static uint16_t bgcolor = tft.Color565(0,200,0);
		static uint16_t bgcolors[16] = {
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
			tft.Color565(0, 150+rand()%45, 0),
		};
		//tft.fillRect(mDirtyRegion.X, mDirtyRegion.Y, mDirtyRegion.W, mDirtyRegion.H, bgcolor);
		tft.setAddrWindow(mDirtyRegion.X, mDirtyRegion.Y, 
			              mDirtyRegion.X + mDirtyRegion.W, 
			              mDirtyRegion.Y + mDirtyRegion.H);
		tft.fastPushColorBegin();
		for (int y = mDirtyRegion.Y; y < mDirtyRegion.H+mDirtyRegion.Y; ++y) {
			for (int x = mDirtyRegion.X; x < mDirtyRegion.W+mDirtyRegion.X; ++x) {
				uint16_t i = x*y;
				tft.fastPushColor(bgcolors[i%13]);
			}
		}
		tft.fastPushColorEnd();

		//draw the deck
		if (!mTopOfDeck || mTopOfDeck->Next)
			drawCardBack(1, 2, NULL, true);
		//handle cursor
		if (mCursorLocationX == 0 && mCursorLocationY == 0)
			//draw the cursor
			drawCursor(1, 2);

		//track where we draw the cursor to draw the to-move cards hovering over it
		int cursorAtX = 0;
		int cursorAtY = 0;

		//draw the revealed deck cards
		Card* cur = mTopOfDeck;
		if (cur) {
			//start with the card up to two cards back
			int cardsToDraw = 1;
			for (int i = 0; i < 2; ++i) {
				if (cur && cur->Prev) {
					cur = cur->Prev;
					++cardsToDraw;
				}
			}
			//
			for (int i = 0; i < cardsToDraw; ++i) {
				if (cur) {
					//draw cur
					drawCard(cur->Which, 22 + 14*i, 2, cur, true);
					//if we're last, draw the draw cursor
					if (mCursorLocationX == 1 && mCursorLocationY == 0 && 
						(i == cardsToDraw-1 || !cur->Next)) 
					{
						drawCursor(22 + 14*i, 2);
						cursorAtX = 22 + 14*i;
						cursorAtY = 2;
					}
					//traverse to the next card
					cur = cur->Next;
				}
			}
		} else {
			//no revealed? Special case for cursor
			if (mCursorLocationX == 1 && mCursorLocationY == 0) {
				drawCursor(22, 2);
				cursorAtX = 22;
				cursorAtY = 2;
			}
		}

		//draw the stacks
		for (int stackN = 0; stackN < 4; ++stackN) {
			cur = mStacks[stackN];
			if (!cur->isempty()) {
				drawCard(cur->Which, 75 + stackN*22, 2, cur, true);
			}
			//draw cursor, whether the stack has cards or not
			if (mCursorLocationY == 0 && mCursorLocationX == stackN+2) {
				drawCursor(75 + stackN*22, 2);
				cursorAtX = 75 + stackN*22;
				cursorAtY = 2;
			}
		}

		//draw the stacks on the board
		for (int stackN = 0; stackN < 7; ++stackN) {
			cur = mBoard[stackN]->Next; //the first entry is the "base"
			int depth = 0;
			int cardN = 0;
			//special case cursor for emyty col
			if (!cur && mCursorLocationX == stackN && mCursorLocationY == 1) {
				drawCursor(3 + 22*stackN, 17);
				cursorAtX = 3 + 22*stackN;
				cursorAtY = 17;
			}
			while (cur) {
				int oldDepth = depth;
				if (cur->FaceUp) {
					drawCard(cur->Which, 3 + 22*stackN, 17 + depth, cur);
					depth += 8;
					++cardN;
				} else {
					drawCardBack(3 + 22*stackN, 17 + depth, cur, cur->Next != 0);
					depth += 4;
					++cardN;
				}
				if (mCursorLocationX == stackN && mCursorLocationY == cardN) {
					drawCursor(3 + 22*stackN, 17 + oldDepth);
					cursorAtX = 3 + 22*stackN;
					cursorAtY = 17 + oldDepth;
				}
				cur = cur->Next;
			}
		}

		//draw the held cards hovering the cursor
		if (mHeldCard) {
			int depth = 0;
			drawCard(mHeldCard->Which, cursorAtX + 7, cursorAtY + 7, mHeldCard);
			drawGrabCursor(cursorAtX + 7, cursorAtY + 7);
			Card* cur = mHeldCard->Next;
			while (cur) {
				++depth;
				drawCard(cur->Which, cursorAtX + 7, cursorAtY + 7 + depth*8, cur);
				cur = cur->Next;
			}
		}

		//set the new dirty rect to where the cursor is to start out with, we
		//always have to update that region. Also add on the held cards if we have
		//some. Other things can be added elsewhere
		mDirtyRegion.X = cursorAtX;
		mDirtyRegion.Y = cursorAtY;
		mDirtyRegion.W = 20;
		mDirtyRegion.H = 29;
		//add on held
		if (mHeldCard) {
			Card* cur = mHeldCard;
			while (cur) {
				mDirtyRegion.expand(cur->LastDrawnAt);
				cur = cur->Next;
			}
		}

	}

	///////////////////////////////////////////////////////////////////////////
	// main action code
	uint8_t getBoardStackSize(uint8_t n) {
		Card* c = mBoard[n]; //will always be non-null
		uint8_t count = 0;
		while (c->Next) {
			count++;
			c = c->Next;
		}
		return count;
	}
	uint8_t toprowXtoBoardX(uint8_t toprowX) {
		return (toprowX == 0) ? 0 : (toprowX+1);
	}
	uint8_t boardXtoToprowX(uint8_t boardX) {
		switch (boardX) {
		case 0: return 0;
		case 1: return 1;
		case 2: return 1;
		default: return boardX-1;
		}
	}
	void moveCursor(int8_t dx, int8_t dy) {
		if (mHeldCard) {
			//move held cards
			//change current target
			if (dx+dy > 0) mCurrentTarget++; else mCurrentTarget--;
			mCurrentTarget = smartmod(mCurrentTarget, mNumValidTargets);
			int8_t currentTargetData = mValidTargets[mCurrentTarget];
			//update cursor location
			mCursorLocationX = currentTargetData>>1;
			mCursorLocationY = currentTargetData&1;
			//change the cursor y to the end of the stack (but still 1 for empty stacks)
			if (mCursorLocationY > 0)
				mCursorLocationY = max(1, getBoardStackSize(mCursorLocationX));

		} else {
			//move cursor
			//move on the y axis
			if (dy != 0) {
				if (mCursorLocationY == 0) {
					mCursorLocationX = toprowXtoBoardX(mCursorLocationX);
					if (dy > 0)
						mCursorLocationY += dy;
					else
						mCursorLocationY = getBoardStackSize(mCursorLocationX); 
				} else {
					//see how big the current stack is
					//smartmod is a mod that "wraps around to max+a" for negative a's
					mCursorLocationY = 
						smartmod(mCursorLocationY+dy, getBoardStackSize(mCursorLocationX)+1);
					if (mCursorLocationY == 0)
						//if we got to 0, switch the x
						mCursorLocationX = boardXtoToprowX(mCursorLocationX);
				}
			}
			if (dx != 0) {
				//change the x
				uint8_t max = (mCursorLocationY==0) ? 5 : 6;
				mCursorLocationX = smartmod(mCursorLocationX+dx, max+1);

				//now check that clamp the y to be within [0, colsize]
				if (mCursorLocationY > 0)
					mCursorLocationY = min(mCursorLocationY, getBoardStackSize(mCursorLocationX));
			}
			//if we are in a stack, adjust the y to not be an unrevealed card
			if (mCursorLocationY > 0) {
				if (dy < 0) {
					//for less than 0, go up to the top row
					mCursorLocationX = boardXtoToprowX(mCursorLocationX);
					mCursorLocationY = 0;
				} else {
					Card* cur = mBoard[mCursorLocationX]->Next;
					int n = 1;
					while (!cur->FaceUp || n < mCursorLocationY) {
						cur = cur->Next;
						++n;
					}
					mCursorLocationY = n;
				}
			}
		}
	}
	void putDownHeldCard() {
		//where to place it?
		if (mCursorLocationY == 0) {
			if (mCursorLocationX == 1) {
				//put back on the deck.
				//insert
				if (mTopOfDeck) {
					mHeldCard->Next = mTopOfDeck->Next;
				} else {
					mHeldCard->Next = mDeck;
					mDeck = mHeldCard;
				}
				mHeldCard->Prev = mTopOfDeck;
				mTopOfDeck->Next->Prev = mHeldCard;
				mTopOfDeck->Next = mHeldCard;
				mTopOfDeck = mHeldCard;
				//change loc / faceup states
				mHeldCard->Location = Card::LocationDeck;
				mHeldCard->FaceUp = false;

			} else if (mCursorLocationX > 1) {
				//put on one of the stacks.
				int stackN = mCursorLocationX - 2;
				Card* base = mStacks[stackN];
				base->Next = mHeldCard;
				mHeldCard->Prev = base;
				mHeldCard->Location = Card::LocationStack;
				mStacks[stackN] = mHeldCard;

			} else {
				error("mCursorLocation = (0,0) with card held");
			}
		} else {
			//put it on the board
			Card* base = mBoard[mCursorLocationX];
			//place at the end of the stack
			while (base->Next) base = base->Next;
			base->Next = mHeldCard;
			mHeldCard->Prev = base;
			//update the location
			Card* cur = mHeldCard;
			while (cur) {
				cur->Location = Card::LocationBoard;
				cur = cur->Next;
			}
		}
		//should we reveal a card?
		//if we have on to reveal, and we didn't place the held card 
		//back on the one to reveal, yes.
		if (mCardToReveal && mCardToReveal != mHeldCard->Prev) {
			mCardToReveal->FaceUp = true;
			//update it
			mDirtyRegion.expand(mCardToReveal->LastDrawnAt);
		}

		//done placing
		mHeldCard = 0;
	}
	void button1Down() {
		if (mHeldCard) {
			putDownHeldCard();
		} else {
			//no held card
			if (mCursorLocationX == 0 && mCursorLocationY == 0) {
				//reveal more
				flip3();
				invalidateDeckRegion(); //need to redraw it fully
			} else {
				//pick up cards
				if (mCursorLocationY == 0) {
					if (mCursorLocationX == 1) {
						//pick up from the deck, needs some special handling to
						//let the user put the card back down there
						Card* card = mTopOfDeck;
						if (card) {
							//un+relink
							Card* oldPrev = card->Prev;
							Card* oldNext = card->Next;
							if (oldPrev) {
								oldPrev->Next = oldNext;
							} else {
								//no old previous, update the deck
								mDeck = card->Next;
							}
							if (oldNext) oldNext->Prev = oldPrev;
							card->Next = 0;
							card->Prev = 0;
							//change the card location / faceup
							card->Location = Card::LocationDrag;
							card->FaceUp = true;
							//modify the top of deck
							mTopOfDeck = oldPrev;
							//pick up
							mHeldCard = card;
							mHeldWasTopOfDeck = true;
							mCardToReveal = 0;
							//dirty the deck region, needs a redraw
							invalidateDeckRegion();
						}

					} else {
						//pick up from the stacks at the top
						Card* stackTop = mStacks[mCursorLocationX-2];
						if (!stackTop->isempty()) {
							//unlink
							stackTop->Prev->Next = 0;
							mStacks[mCursorLocationX-2] = stackTop->Prev;
							stackTop->Prev = 0;
							//change the card location / faceup
							stackTop->Location = Card::LocationDrag;
							stackTop->FaceUp = true;
							//pick up
							mHeldCard = stackTop;
							mHeldWasTopOfDeck = false;
							mCardToReveal = 0;
						}
					}
				} else {
					//pick up from the piles
					Card* card = mBoard[mCursorLocationX]->Next;
					if (card) {
						//we have a card in the stack

						//find the actual card
						for (int n = 1; n < mCursorLocationY; ++n)
							card = card->Next;

						//Now, *CZZzXzx* Pick up that can!... I mean card!
						//unlink
						Card* oldPrev = card->Prev;
						card->Prev->Next = 0;
						card->Prev = 0;
						//change stack location / faceup
						Card* cur = card;
						while (cur) {
							cur->Location = Card::LocationDrag;
							cur = cur->Next;
						}
						//pick up
						mHeldCard = card;
						mHeldWasTopOfDeck = false;
						if (oldPrev->isempty())
							mCardToReveal = 0;
						else
							mCardToReveal = oldPrev;
						//when we pick up a card from the stack, move the cursor 
						//1 up to the card that was under it, but only if y > 1
						if (mCursorLocationY > 1)
							--mCursorLocationY;
					}
				} 
			}
			//if we picked up a card
			if (mHeldCard) {
				//determine where it can be put down, and populate the validtargets array
				mNumValidTargets = 0;
				mCurrentTarget = 0;
				//first add the deck
				if (mHeldWasTopOfDeck) {
					//then we can put on the top of the deck
					mValidTargets[mNumValidTargets++] = (1<<1) | 0;
					mCurrentTarget = 0;
				}
				//next, add the stacks, but only if we're dragging one card
				if (!mHeldCard->Next) {
					for (int i = 0; i < 4; ++i) {
						Card* stack = mStacks[i];
						if (stack->isempty() ? 
							(mHeldCard->Which.getNumber() == CardId::NumAce) :
							(mHeldCard->Which.getNumber() == stack->Which.getNumber()+1 &&
							 mHeldCard->Which.getSuit() == stack->Which.getSuit())) {
							if (mCursorLocationX == i+2 && mCursorLocationY == 0)
								mCurrentTarget = mNumValidTargets;
							mValidTargets[mNumValidTargets++] = (((i+2)<<1) | 0);
						}
					}
				}
				//finally, add the board
				for (int i = 0; i < 7; ++i) {
					//find end of board stack
					Card* cur = mBoard[i];
					while (cur->Next) cur = cur->Next;

					//add as target location if matching
					if (cur == mCardToReveal || 
						(cur->isempty() && mCursorLocationX == i && mCursorLocationY == 1) ||
						(cur->isempty() && mHeldCard->Which.getNumber() == CardId::NumKing) ||
						((cur->Which.getColor() != mHeldCard->Which.getColor()) && 
						 (cur->Which.getNumber() == mHeldCard->Which.getNumber()+1))) {
						//valid for where we picked up off of, king->empty, or color->other color
						mValidTargets[mNumValidTargets++] = (i<<1) | 1;
					}
				}

				if (mNumValidTargets == 0)
					error("Picked up, but no valid targets?");

				//if there was only one valid spot, don't pick up anything, just put the cards right
				//back down
				if (mNumValidTargets == 1)
					putDownHeldCard();

			}
			//if we got a held stack, invalidate those cards
			Card* cur = mHeldCard;
			while (cur) {
				mDirtyRegion.expand(cur->LastDrawnAt);
				cur = cur->Next;
			}
		}
	}

private:
	// Y = 0 :  X=  0 => deck, 1 => dealt, 2-5 => stacks 
	// Y = 1+:  X=  0-6 => board
	uint8_t mCursorLocationX;
	uint8_t mCursorLocationY;
	//
	bool mHasHeldCards;  //we are moving some cards
	Card* mHeldCard;     //the cards to move
	bool mHeldWasTopOfDeck; //flag to know whether the picked up card is allowed to be
	                        //placed back on the deck
	Card* mCardToReveal; //card at the top of a stack to reveal on the cards being put down
	//if we have a move, what locations could the cards being moved be placed
	//at? There are at most 4+7+1 = 12 locations, so we can use a constant
	//sized array to store them.
	//They are packed as CursorY = v&1, CursorX = v>>1
	int8_t mCurrentTarget;
	uint8_t mNumValidTargets;
	uint8_t mValidTargets[12];
	//drawing stuff
	Rect mDirtyRegion;
	//
	Deck mSourceDeck;
	Card* mDeck;
	Card* mTopOfDeck;
	Card* mStacks[4]; //pointer to the top card in a stack
	Card* mBoard[7];  //pointer to the bottom card in a stack on the broad
	//
	Card mBoardBases[7];
	Card mStackBases[4];
	//
	uint16_t mSelectedColor;
	uint16_t mGrabColor;
} GameState;


///////////////////////////////////////////////////////////////////////////////
void setup() {
	//std::cout << sizeof(Card) << "\n";
	Serial.begin(9600);
	tft.initR(INITR_REDTAB);   // initialize a ST7735R chip, red tab
	tft.setRotation(1);

	// //
	// tft.fillScreen(tft.Color565(0,200,0));
	// DrawCardBack(3, 2, true);
	// DrawCard(CardId::RandomCard(), 25, 2, true);
	// for (int i = 0; i < 7; ++i) {
	// 	DrawCard(CardId::RandomCard(), 3 + 22*i, 17);
	// }
	// for (int i = 0; i < 13; ++i) {
	// 	DrawCard(CardId::RandomCard(), 3 + 22*6, 17+8*i);
	// }

	GameState.initialize();
	GameState.flip3();
	GameState.draw();

	long lastMoveAt = 0;
	bool lastButtonState = false;
	int joyBaseY = analogRead(0);
	int joyBaseX = analogRead(1);
	pinMode(9, INPUT_PULLUP);
	pinMode(14, INPUT_PULLUP);
	//
	while (true) {
		long now = millis();
		if (now-lastMoveAt > 400) {
			int dy = -(analogRead(0)-joyBaseY);
			int dx = analogRead(1)-joyBaseX;
			if (abs(dy) > 35) {
				dy = (dy>0) ? 1 : -1;
			} else {
				dy = 0;
			}
			if (abs(dx) > 35) {
				dx = (dx>0) ? 1 : -1;
			} else {
				dx = 0;
			}
			if (dx!=0 || dy!=0) {
				//do move
				lastMoveAt = now;
				GameState.moveCursor(dx, dy);
				GameState.draw();
			}
		}
		if (!lastButtonState && !digitalRead(9)) {
			lastButtonState = true;
			GameState.button1Down();
			GameState.draw();
		} else if (lastButtonState && digitalRead(9)) {
			lastButtonState = false;
		}
		if (!digitalRead(14)) {
			delay(500);
			GameState.initialize();
			GameState.flip3();
			GameState.draw();
		}
	}
}

void loop() {}












