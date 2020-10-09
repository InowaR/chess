#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>

/*
typedef int bool;
enum {
	false = 0,
	true = 1
};
*/
/* Using enum to define constants instead of #define. You can't use "const int" in C, only in C++. */


enum {
	STRIDE = (8 + 2),
	BOARD_SIZE = (8 + 4) * STRIDE,
	RANK_1 = STRIDE * 2,
	RANK_2 = STRIDE * 3,
	RANK_3 = STRIDE * 4,
	RANK_4 = STRIDE * 5,
	RANK_5 = STRIDE * 6,
	RANK_6 = STRIDE * 7,
	RANK_7 = STRIDE * 8,
	RANK_8 = STRIDE * 9,

	OFFBOARD = -1,
	EMPTY = 0,
	PAWN = 1,
	ROOK = 2,
	KNIGHT = 3,
	BISHOP = 4,
	QUEEN = 5,
	KING = 6,
	BLACK = 8,
	WHITE = 16,										/* We use two bits for color. 00 = empty, 01 = black, 10 = white, 11 = off-board. This allows fast bit-tricks for tracing possible moves. E.g. (... & color) == 0 to check if a square is empty or contains an opponent piece. */
	PIECE_MASK = 7,
	COLOR_MASK = BLACK | WHITE,
	COLOR_SWITCH_XOR = BLACK ^ WHITE,
	MOVED_MASK = 32,								/* The moved bit helps us determine if a king can castle or not and also if a pawn is allowed to move two squares. */
	PIECE_AND_MOVED_MASK = PIECE_MASK | MOVED_MASK,

	MIN_DEPTH = 4,
	MAX_DEPTH = 16,
	QUIESCENCE_DEPTH = 8,
	TO_SHIFT = 8,
	SCORE_SHIFT = 16,
	MOVE_MASK = 0x0000FFFF,
	FROM_MASK = 0x000000FF,
	TO_MASK = 0x0000FF00,
	SCORE_MASK = ~MOVE_MASK,
	CHECKMATE = 0xFFFF,
	STALEMATE = 0xFFFE,
	IN_CHECK = 0xFFFD,
	VALID_MOVE = 0xFFFC,
	INVALID_MOVE = -1,
	MAX_SCORE = 10000,
	MIN_SCORE = -MAX_SCORE,
	PAWN_VALUE = 100,
	QUEEN_VALUE = 9 * PAWN_VALUE,
	PROMOTION_SCORE = QUEEN_VALUE - PAWN_VALUE,
	CAPTURE_KING_MIN_SCORE = MAX_SCORE - MAX_DEPTH,
	RANDOM_AMOUNT = PAWN_VALUE / 10,
	PAWN_ADVANCE_SCORE = PAWN_VALUE / 10,
	CASTLING_SCORE = PAWN_VALUE / 2,
	CASTLING_MASK = 0x80, 								/* Used for xCapture */
	HISTORY_SIZE = 256
};

const int DIRS[8] = { STRIDE, STRIDE + 1, 1, -STRIDE + 1, -STRIDE, -STRIDE - 1, -1, STRIDE - 1 };
const int KNIGHT_MOVES[8] = { STRIDE * 2 + 1, 2 + STRIDE, 2 - STRIDE, -STRIDE * 2 + 1, -STRIDE * 2 - 1, -STRIDE - 2, STRIDE - 2, STRIDE * 2 - 1 };
const int PIECE_VALUES[7] = { 0, PAWN_VALUE, 5 * PAWN_VALUE, 3 * PAWN_VALUE, 3 * PAWN_VALUE, QUEEN_VALUE, MAX_SCORE };
const int RANK_VALUES[8 + 4]= { 0, 0, 0, 30, 50, 60, 60, 50, 30, 0, 0, 0 }; 	/* Simple score structure used for all pieces but pawns. */

enum {
	MAX_TARGET_SQUARES = 32, 	/* Max target squares for a single piece. */
	MAX_MOVES = 256,			/* Max psuedo-legal moves from any given position. 256 should be enough for any normal game. Prove me wrong. */
	MAX_CAPTURES = 32 			/* Max pseudo-legal captures from any given position. 32 should be enough for any normal game. Prove me wrong. */
};

/* Notice the flipped effect because we have the lowest rank first in the array. */
const int INIT_BOARD[BOARD_SIZE] = {
	OFFBOARD,	OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,
	OFFBOARD,	OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,
	OFFBOARD,	ROOK | WHITE,	KNIGHT | WHITE,	BISHOP | WHITE,	QUEEN | WHITE,	KING | WHITE,	BISHOP | WHITE,	KNIGHT | WHITE,	ROOK | WHITE,	OFFBOARD,
	OFFBOARD,	PAWN | WHITE,	PAWN | WHITE,	PAWN | WHITE,	PAWN | WHITE,	PAWN | WHITE,	PAWN | WHITE,	PAWN | WHITE,	PAWN | WHITE,	OFFBOARD,
	OFFBOARD,	EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			OFFBOARD,
	OFFBOARD,	EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			OFFBOARD,
	OFFBOARD,	EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			OFFBOARD,
	OFFBOARD,	EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			EMPTY,			OFFBOARD,
	OFFBOARD,	PAWN | BLACK,	PAWN | BLACK,	PAWN | BLACK,	PAWN | BLACK,	PAWN | BLACK,	PAWN | BLACK,	PAWN | BLACK,	PAWN | BLACK,	OFFBOARD,
	OFFBOARD,	ROOK | BLACK,	KNIGHT | BLACK,	BISHOP | BLACK,	QUEEN | BLACK,	KING | BLACK,	BISHOP | BLACK,	KNIGHT | BLACK,	ROOK | BLACK,	OFFBOARD,
	OFFBOARD,	OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,
	OFFBOARD,	OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD,		OFFBOARD
};

const char* PIECE_CHARS = " PRNBQK  prnbqk ";
const char* PIECE_NAMES[8] = { "", "pawn", "rook", "knight", "bishop", "queen", "king" };

int board[BOARD_SIZE];
int nextPiece[BOARD_SIZE];
int prevPiece[BOARD_SIZE];
int xCapture;
int turnColor;
int boardHash;

/*
	Unlike most transposition table implementations our hash table is used only for improving move ordering based on
	the outcome of previous searches. This greatly reduces processing time due to alpha beta pruning. It is sufficient
	with a fairly small table (minimum is 64k entries). A bigger table (say 1M entries) gives marginally better results
	with deeper searches. The hash table contains 32-bit ints where the higher 16 bits are the highest 16 bits of the
	hash key (used to minimize collisions) and the lower 16 bits define the last chosen move for this particular
	position.

	Hashes for positions are incrementally updated with a Zobrist mechanism using a table with random values for each
	piece (incl colors and moved bits) and square + xCapture and turnColor values. We bit-and all hash operations with
	~0 to emphasize the "wrapping" of additions and subtractions.

	The advantage of this solution compared to a standard transposition table with exact search results are simplicity
	(no structs needed), reliablitiy (collisions are harmless) and memory footprint.
*/
enum {
	HASH_TABLE_SIZE = 64 * 1024,
	HASH_MASK = ~0
};
int pieceKeys[BOARD_SIZE * 64];
int xCaptureKeys[256];
int colorKeys[WHITE + 1];
int hashTable[HASH_TABLE_SIZE];
int evalCounter = 0;
int history[HISTORY_SIZE * 4];		/* Four elements per half-move: 16-bit move, piece, captured and xCapture (see doMove() for more info). */
int historyCount = 0;
int minEvals = 10000;

unsigned int randPX = 123456789;
unsigned int randPY = 362436069;

unsigned int nextRandom() {

	/* Marsaglia's Xorshift PRNG algorithm. */
	
	unsigned int t = randPX ^ (randPX << 10);
	randPX = randPY;
	randPY = randPY ^ (randPY >> 13) ^ t ^ (t >> 10);
	return randPY;
}

void initGlobals() {	
	/*
		The xor-shift random algorithm makes it necessary to use addition and subtraction for the incremental Zobrist
		hashing instead of xor.
	*/
	
	int i;
	for (i = 0; i < 64 * BOARD_SIZE; ++i) {
		pieceKeys[i] = nextRandom();
	}
	for (i = 0; i < 256; ++i) {
		xCaptureKeys[i] = nextRandom();
	}
	colorKeys[WHITE] = nextRandom();
	colorKeys[BLACK] = nextRandom();
}

bool isOnBoard(int square) {
	return (square >= 0 && square < BOARD_SIZE && INIT_BOARD[square] != OFFBOARD);
}

bool isValidColor(int color) {
	return (color == WHITE || color == BLACK);
}

bool isValidMove(int move) {
	return ((move & MOVE_MASK) == move && isOnBoard(move & FROM_MASK) && isOnBoard((move & TO_MASK) >> TO_SHIFT));
}

bool isValidPiece(int piece) {
	return (1 <= piece && piece < 64 && (piece & PIECE_MASK) >= 1 && (piece & PIECE_MASK) <= 6
			&& isValidColor(piece & COLOR_MASK));
}

int moveFrom(int move) {
	assert(isValidMove(move));
	return (move & FROM_MASK);
}

int moveTo(int move) {
	assert(isValidMove(move));
	return move >> TO_SHIFT;
}

int makeMove(int fromCol, int fromRow, int toCol, int toRow) {
	return ((fromRow + 2) * STRIDE + fromCol + 1) | (((toRow + 2) * STRIDE + toCol + 1) << TO_SHIFT);
}

int switchColor(int color) {
	return color ^ COLOR_SWITCH_XOR;
}

bool checkInvariant() {
	int color, h, i, counts[(BLACK | WHITE) + 1], countsAgain[(BLACK | WHITE) + 1];

	assert(isValidColor(turnColor));

	if (xCapture != 0) {
		assert(0 <= xCapture && xCapture < 256);
		assert(isOnBoard(xCapture & ~128));
		
		if (((xCapture & CASTLING_MASK) != 0)) {
			assert((board[xCapture & ~CASTLING_MASK] & PIECE_AND_MOVED_MASK) == (ROOK | MOVED_MASK));
			assert((board[(xCapture & ~CASTLING_MASK) - 1] & PIECE_AND_MOVED_MASK) == (KING | MOVED_MASK)
					|| (board[(xCapture & ~CASTLING_MASK) + 1] & PIECE_AND_MOVED_MASK) == (KING | MOVED_MASK));
		} else {
			assert((xCapture >= RANK_3 && xCapture < RANK_4) || (xCapture >= RANK_6 && xCapture < RANK_7));
			assert(board[xCapture + STRIDE] == (WHITE | PAWN | MOVED_MASK)
					|| board[xCapture - STRIDE] == (BLACK | PAWN | MOVED_MASK));
		}
	}
	
	color = BLACK;
	do {
		int from = nextPiece[color];
		counts[color] = 0;
		while (from != color) {
			int p = board[from];
			(void)p;
			assert((p & COLOR_MASK) == color);
			assert(isValidPiece(p));
			from = nextPiece[from];
			++counts[color];
		}
		color = switchColor(color);
	} while (color != BLACK);
	
	h = 0;
	countsAgain[BLACK] = countsAgain[WHITE] = 0;
	for (i = 0; i < BOARD_SIZE; ++i) {
		int p = board[i];
		assert(INIT_BOARD[i] != OFFBOARD || p == OFFBOARD);
		if (p > EMPTY) {
			assert(isValidPiece(p));
			assert(0 <= p && p < 64);
			h = (h + pieceKeys[i * 64 + p]) & HASH_MASK;
		}
		++countsAgain[p & COLOR_MASK];
	}
	
	assert(countsAgain[BLACK] == counts[BLACK]);
	assert(countsAgain[WHITE] == counts[WHITE]);
	assert(h == boardHash);
	
	return true;
}

void putPiece(int square, int piece) {
	int c, n;

	assert(isOnBoard(square));
	assert(isValidPiece(piece));
	
	assert(board[square] == EMPTY);
	board[square] = piece;
	c = piece & COLOR_MASK;
	n = nextPiece[c];
	nextPiece[square] = n;
	prevPiece[square] = c;
	prevPiece[n] = square;
	nextPiece[c] = square;
	boardHash = (boardHash + pieceKeys[square * 64 + piece]) & HASH_MASK;
}

void liftPiece(int square) {
	int piece, n, p;

	assert(isOnBoard(square));
	
	piece = board[square];
	assert(piece > EMPTY);
	boardHash = (boardHash - pieceKeys[square * 64 + piece]) & HASH_MASK;
	board[square] = EMPTY;
	n = nextPiece[square];
	p = prevPiece[square];
	nextPiece[p] = n;
	prevPiece[n] = p;
}

void movePiece(int from, int to, int newPiece) {
	int oldPiece, n, p;

	assert(isOnBoard(from));
	assert(isOnBoard(to));
	assert(isValidPiece(newPiece));
	assert(from != to);	
	assert(board[to] == EMPTY);
	
	board[to] = newPiece;
	oldPiece = board[from];
	assert(isValidPiece(oldPiece));
	boardHash = (boardHash + pieceKeys[to * 64 + newPiece] - pieceKeys[from * 64 + oldPiece]) & HASH_MASK;
	board[from] = EMPTY;
	n = nextPiece[from];
	p = prevPiece[from];
	prevPiece[to] = p;
	nextPiece[to] = n;
	nextPiece[p] = to;
	prevPiece[n] = to;
}

void restart() {
	int i;

	nextPiece[WHITE] = prevPiece[WHITE] = WHITE;
	nextPiece[BLACK] = prevPiece[BLACK] = BLACK;
	boardHash = 0;
	for (i = 0; i < BOARD_SIZE; ++i) {
		board[i] = EMPTY;
		if (INIT_BOARD[i] == OFFBOARD) {
			board[i] = OFFBOARD;
		} else if (INIT_BOARD[i] > EMPTY) {
			putPiece(i, INIT_BOARD[i]);
		}
	}
	for (i = 0; i < HASH_TABLE_SIZE; ++i) {
		hashTable[i] = 0;
	}
	turnColor = WHITE;
	xCapture = 0;
	historyCount = 0;
	assert(checkInvariant());
}

bool squareCapturesCastlingKing(int square) {

	/*
		On the ply immediately following castling, xCapture will contain the rook's square (+ CASTLING_MASK) and any
		move to this square or the horizontally adjacent squares are interpreted as capturing the king in order to
		prevent illegal castling.
	*/
	
	return ((square | CASTLING_MASK) >= xCapture - 1 && (square | CASTLING_MASK) <= xCapture + 1);
}

bool squareCapturesKing(int square) {
	assert(isOnBoard(square));

	return ((board[square] & PIECE_MASK) == KING || squareCapturesCastlingKing(square));
}

/*
	doMove() could have returned all required data for undoMove() but in order to avoid language dependent constructs
	for returning multiple ints (such as structs and pointers), you setup the necessary undo data before calling
	doMove() instead. Like this:

	int oldPiece = board[from];
	int captured = board[to];
	int oldXCapture = xCapture;

	Then use the same arguments for undoMove()

	doMove returns the differential score for this move, including a small random amount to avoid repetition.
*/

int doMove(int from, int to, int oldPiece, int captured, int oldXCapture) {
	int newPiece, score;

	assert(isOnBoard(from));
	assert(isOnBoard(to));
	assert(isValidPiece(oldPiece));
	assert(captured == EMPTY || isValidPiece(captured));
	assert(from != to);

	assert(checkInvariant());
	
	newPiece = oldPiece;
	score = (nextRandom() & 7) - 4;	
	xCapture = 0;

	switch (oldPiece) {
		case KING | WHITE: {
			switch (to) {
				case RANK_1 + 3: {
					
					/* White castling queenside */

					movePiece(RANK_1 + 1, RANK_1 + 4, ROOK | WHITE | MOVED_MASK);
					xCapture = (RANK_1 + 4) | CASTLING_MASK;
					score += CASTLING_SCORE;
					break;
				}
				
				case RANK_1 + 7: {

					/* White castling kingside */

					movePiece(RANK_1 + 8, RANK_1 + 6, ROOK | WHITE | MOVED_MASK);
					xCapture = (RANK_1 + 6) | CASTLING_MASK;
					score += CASTLING_SCORE;
					break;
				}
				
				default: {
					score += RANK_VALUES[to / STRIDE] - RANK_VALUES[from / STRIDE];
					break;
				}
			}
			break;
		}
		
		case KING | BLACK: {
						
			switch (to) {
				case RANK_8 + 3: {

					/* Black castling queenside */

					movePiece(RANK_8 + 1, RANK_8 + 4, ROOK | BLACK | MOVED_MASK);
					xCapture = (RANK_8 + 4) | CASTLING_MASK;
					score += CASTLING_SCORE;
					break;
				}
				
				case RANK_8 + 7: {

					/* Black castling kingside */

					movePiece(RANK_8 + 8, RANK_8 + 6, ROOK | BLACK | MOVED_MASK);
					xCapture = (RANK_8 + 6) | CASTLING_MASK;
					score += CASTLING_SCORE;
					break;
				}
				
				default: {
					score += RANK_VALUES[to / STRIDE] - RANK_VALUES[from / STRIDE];
					break;
				}
			}			
			break;
		}
		
		case PAWN | WHITE | MOVED_MASK: {
			if (to == oldXCapture) {
				
				/* En passant capture black pawn */
				
				assert(board[to - STRIDE] == (PAWN | BLACK | MOVED_MASK));
				liftPiece(to - STRIDE);
				score += PAWN_ADVANCE_SCORE + PAWN_VALUE;
				
			} else if (to >= RANK_8) {
				
				/* Promotion (always to white queen) */
				
				newPiece = QUEEN | WHITE | MOVED_MASK;
				score += PROMOTION_SCORE;
				
			} else {
				score += PAWN_ADVANCE_SCORE;
			}
			break;
		}
		
		case PAWN | BLACK | MOVED_MASK: {
			if (to == oldXCapture) {
			
				/* En passant capture white pawn */
			
				assert(board[to + STRIDE] == (PAWN | WHITE | MOVED_MASK));
				liftPiece(to + STRIDE);
				score += PAWN_ADVANCE_SCORE + PAWN_VALUE;
			} else if (to < RANK_2) {
			
				/* Promotion (always to black queen) */
				
				newPiece = QUEEN | BLACK | MOVED_MASK;
				score += PROMOTION_SCORE;
				
			} else {
				score += PAWN_ADVANCE_SCORE;
			}
			break;
		}
		
		case PAWN | WHITE: {
			if (to >= RANK_4) {
				
				/* Register en passant capture square (white) */
				
				xCapture = to - STRIDE;	
				score += PAWN_ADVANCE_SCORE;
			}
			score += PAWN_ADVANCE_SCORE;
			break;
		}
		
		case PAWN | BLACK: {
			if (to < RANK_6) {
				
				/* Register en passant capture square (black) */
				
				xCapture = to + STRIDE;
				score += PAWN_ADVANCE_SCORE;
			}
			score += PAWN_ADVANCE_SCORE;
			break;
		}

		default: {
			score += RANK_VALUES[to / STRIDE] - RANK_VALUES[from / STRIDE];
			break;
		}
	}
	
	if (captured > EMPTY) {
		liftPiece(to);
	}
	movePiece(from, to, newPiece | MOVED_MASK);
	
	/*
		Capturing gives a score based on piece type and rank. Without including rank, the computer would value moves
		that end in definite captures just for the score from the move itself.
	*/
	
	switch (captured & (PIECE_MASK | COLOR_MASK)) {
		case EMPTY: break;
		
		case PAWN | WHITE: {
			score += PAWN_VALUE + (to / STRIDE - 3) * PAWN_ADVANCE_SCORE;
			break;
		}
		
		case PAWN | BLACK: {
			score += PAWN_VALUE + (8 - to / STRIDE) * PAWN_ADVANCE_SCORE;
			break;
		}
		
		default: {
			score += PIECE_VALUES[captured & PIECE_MASK] + RANK_VALUES[to / STRIDE];
			break;
		}
	}
	
	turnColor = switchColor(turnColor);
	
	assert(checkInvariant());
	
	return score;
}

/* See doMove() for parameters. */

void undoMove(int from, int to, int oldPiece, int captured, int oldXCapture) {
	assert(isOnBoard(from));
	assert(isOnBoard(to));
	assert(isValidPiece(oldPiece));
	assert(captured == EMPTY || isValidPiece(captured));
	assert(from != to);

	assert(checkInvariant());

	xCapture = oldXCapture;
	putPiece(from, oldPiece);
	liftPiece(to);
	if (captured > EMPTY) {
		putPiece(to, captured);
	}
	
	switch (oldPiece) {
		case KING | WHITE: {
			switch (to) {
				case RANK_1 + 3: {
					
					/* Undo white castling queenside */

					movePiece(RANK_1 + 4, RANK_1 + 1, ROOK | WHITE);
					break;
				}
				
				case RANK_1 + 7: {

					/* Undo white castling kingside */

					movePiece(RANK_1 + 6, RANK_1 + 8, ROOK | WHITE);
					break;
				}
			}
			break;
		}
		
		case KING | BLACK: {
			switch (to) {
				case RANK_8 + 3: {
				
					/* Undo black castling queenside */

					movePiece(RANK_8 + 4, RANK_8 + 1, ROOK | BLACK);
					break;
				}
				
				case RANK_8 + 7: {

					/* Undo black castling kingside */

					movePiece(RANK_8 + 6, RANK_8 + 8, ROOK | BLACK);
					break;
				}
			}
			break;
		}
		
		case PAWN | WHITE | MOVED_MASK: {
			if (to == oldXCapture) {
			
				/* Undo en passant capture (white) */
				
				putPiece(to - STRIDE, PAWN | BLACK | MOVED_MASK);
			}
			break;
		}
		
		case PAWN | BLACK | MOVED_MASK: {
			if (to == oldXCapture) {
			
				/* Undo en passant capture (black) */

				putPiece(to + STRIDE, PAWN | WHITE | MOVED_MASK);
			}
			break;
		}
	}

	turnColor = switchColor(turnColor);

	assert(checkInvariant());
}

int traceRBQ(int color, int from, int toCount, int tos[], int start, int step) {
	int cx, d;

	assert(isOnBoard(from));

	/* Notice how we don't check squareCapturesCastlingKing() because we will stop tracing at first / last rank anyhow.*/

	cx = switchColor(color);
	for (d = start; d < 8; d += step) {
		int toInc = DIRS[d];
		int to;
		for (to = from + toInc; board[to] == EMPTY; to += toInc) {
			tos[toCount++] = to;
		}
		if ((board[to] & COLOR_MASK) == cx) {
			tos[toCount++] = to;
		}
	}
	return toCount;
}

int listMoves(int color, int from, int tos[]) {
	int toCount, to, piece, cx;

	assert(isOnBoard(from));

	toCount = 0;
	piece = board[from];
	switch (piece & PIECE_MASK) {
		case PAWN: {
			int s = (color == BLACK ? -STRIDE : STRIDE);
			to = from + s;
			if (board[to] == EMPTY) {
				tos[toCount++] = to;
				if ((piece & MOVED_MASK) == 0 && board[to + s] == EMPTY) {
					tos[toCount++] = to + s;
				}
			}
			cx = switchColor(color);
			--to;
			if ((board[to] & COLOR_MASK) == cx || xCapture == to || squareCapturesCastlingKing(to)) {
				tos[toCount++] = to;
			}
			to += 2;
			if ((board[to] & COLOR_MASK) == cx || xCapture == to || squareCapturesCastlingKing(to)) {
				tos[toCount++] = to;
			}
			break;
		}
		
		case ROOK: toCount = traceRBQ(color, from, toCount, tos, 0, 2); break;
		case BISHOP: toCount = traceRBQ(color, from, toCount, tos, 1, 2); break;
		case QUEEN: toCount = traceRBQ(color, from, toCount, tos, 0, 1); break;
		
		case KING: {

			/* Notice how we don't check squareCapturesCastlingKing() because we will stop tracing at first / last rank anyhow. */
	
			int d;
			for (d = 0; d < 8; ++d) {
				to = from + DIRS[d];
				if ((board[to] & color) == 0) {
					tos[toCount++] = to;
				}
			}
			if ((piece & MOVED_MASK) == 0) {
				
				/* Castling */
				
				to = (color == BLACK ? RANK_8 : RANK_1);
				if ((board[to + 2] | board[to + 3] | board[to + 4]) == EMPTY
						&& (board[to + 1] & PIECE_AND_MOVED_MASK) == ROOK) {
					tos[toCount++] = to + 3;
				}
				if ((board[to + 7] | board[to + 6]) == EMPTY
						&& (board[to + 8] & PIECE_AND_MOVED_MASK) == ROOK) {
					tos[toCount++] = to + 7;
				}
			}
			break;
		}
		
		case KNIGHT: {

			/* Notice how we don't check squareCapturesCastlingKing() because we will stop tracing at first / last rank anyhow. */

			int d;
			for (d = 0; d < 8; ++d) {
				to = from + KNIGHT_MOVES[d];
				if ((board[to] & color) == 0) {
					tos[toCount++] = to;
				}
			}
			break;
		}
		
		default: assert(0);
	}
	
	assert(toCount <= MAX_TARGET_SQUARES);
	return toCount;
}

bool canCaptureKing(int color) {
	int tos[MAX_TARGET_SQUARES];
	int from = nextPiece[color];

	assert(isValidColor(color));

	while (from != color) {
		int toCount = listMoves(color, from, tos);
		int i;
		for (i = 0; i < toCount; ++i) {
			if (squareCapturesKing(tos[i])) {
				return true;
			}
		}
		from = nextPiece[from];
	}
	return false;
}

int search(int alpha, int beta, int depth, int score, bool capturesOnly);

int findBestMove(int lowestScore, int alpha, int beta, int depth, int score, int movesCount, int moves[]) {
	int bestScore = lowestScore;
	int bestMove = 0;
	int i;
	for (i = 0; i < movesCount && alpha < beta; ++i) {
		int move = moves[i];
		int from = moveFrom(move);
		int to = moveTo(move);
		int oldPiece = board[from];
		int captured = board[to];
		int oldXCapture = xCapture;
		int moveScore = doMove(from, to, oldPiece, captured, oldXCapture);
		
		int s = score + moveScore;
		if (depth > 0) {
			s = -(search(-beta, -alpha, depth - 1, -s, depth < QUIESCENCE_DEPTH) >> SCORE_SHIFT);
		}
		if (bestScore < s) {
			bestScore = s;
			bestMove = move;
			if (alpha < bestScore) {
				alpha = bestScore;
			}
		}
		
		undoMove(from, to, oldPiece, captured, oldXCapture);
	}
	return (bestScore << SCORE_SHIFT) | bestMove;
}

int searchCaptures(int alpha, int beta, int depth, int score, int firstMove) {
	int capturesCount, from, moves[MAX_MOVES], tos[MAX_TARGET_SQUARES];

	assert(0 <= depth);
	assert(firstMove == 0 || isValidMove(firstMove));
		
	if (alpha < score) {
		alpha = score;
	}
	
	if (alpha >= beta) {
		return (score << SCORE_SHIFT);
	}

	capturesCount = 0;
	from = nextPiece[turnColor];
	while (from != turnColor) {
		int toCount = listMoves(turnColor, from, tos);
		int i;
		for (i = 0; i < toCount; ++i) {
			int to = tos[i];
			int move = from | (to << TO_SHIFT);

			if (squareCapturesKing(to)) {
				return ((CAPTURE_KING_MIN_SCORE + depth) << SCORE_SHIFT) | move;
			}
			
			if (board[to] > EMPTY || xCapture == to) {
				assert(capturesCount < MAX_MOVES);
				if (move == firstMove) {
					moves[capturesCount++] = moves[0];
					moves[0] = move;
				} else {
					moves[capturesCount++] = move;
				}
			}
		}
		from = nextPiece[from];
	}
	
	return findBestMove(score, alpha, beta, depth, score, capturesCount, moves);
}

int searchAll(int alpha, int beta, int depth, int score, int firstMove) {
	int moves[MAX_MOVES], tos[MAX_TARGET_SQUARES], movesCount, capturesCount, from;

	assert(0 <= depth);
	assert(firstMove == 0 || isValidMove(firstMove));

	if (alpha >= beta) {
	
		/* Return dummy minimum value if alpha beta window has closed. */
		
		return (MIN_SCORE - 1) << SCORE_SHIFT;
	}

	/*
		Moves that capture will be placed before others in the array, and "firstMove" will be moved to the first
		element. This improves alpha beta pruning without sacrificing too much time on sorting.
	*/

	movesCount = 0;
	capturesCount = 0;
	from = nextPiece[turnColor];
	while (from != turnColor) {
		int toCount = listMoves(turnColor, from, tos);
		int i;
		for (i = 0; i < toCount; ++i) {
			int to = tos[i];
			int move = from | (to << TO_SHIFT);

			if (squareCapturesKing(to)) {
				
				/*
					We add depth to the score to make sure we reward early check-mates (higher depth = closer in time).
					This is very important due to the "horizon" effect.
				*/
				
				return ((CAPTURE_KING_MIN_SCORE + depth) << SCORE_SHIFT) | move;
			}
			
			assert(movesCount < MAX_MOVES);
			if (move == firstMove) {
				moves[movesCount++] = moves[capturesCount];
				assert(capturesCount < MAX_MOVES);
				moves[capturesCount++] = moves[0];
				moves[0] = move;
			} else if (board[to] > EMPTY || xCapture == to) {
				moves[movesCount++] = moves[capturesCount];
				assert(capturesCount < MAX_MOVES);
				moves[capturesCount++] = move;
			} else {
				moves[movesCount++] = move;
			}
		}
		from = nextPiece[from];
	}
	
	return findBestMove((MIN_SCORE - 1), alpha, beta, depth, score, movesCount, moves);
}

int search(int alpha, int beta, int depth, int score, bool capturesOnly) {
	int h, hashIndex, entry, firstMove, result, best, bestMove;

	assert(0 <= depth);
	assert(checkInvariant());

	if (beta > CAPTURE_KING_MIN_SCORE + depth) {
		
		/*
			Adjust beta if necessary to force search to end early on all checkmates (since a checkmate score changes
			depending on depth).
		*/
		
		beta = CAPTURE_KING_MIN_SCORE + depth;
	}
	
	/*
		Locate hash entry for this position using the low bits of the key and if the high bits match, begin the search
		with the previously registered best move for this position. This will speed up the process considerably due to
		alpha beta pruning.
	*/

	h = (boardHash + xCaptureKeys[xCapture] + colorKeys[turnColor]) & HASH_MASK;
	hashIndex = (h & (HASH_TABLE_SIZE - 1));
	h = (h & ~MOVE_MASK);
	entry = hashTable[hashIndex];
	firstMove = 0;
	if ((entry & ~MOVE_MASK) == h) {
		firstMove = entry & MOVE_MASK;
	}
	
	if (capturesOnly) {
		best = searchCaptures(alpha, beta, depth, score, firstMove);
		result = best;
		
		/* We can't tell if we have a checkmate just be looking at captures. Doesn't matter. We'll see them soon. */
		
	} else {
		int s;

		best = searchAll(alpha, beta, depth, score, firstMove);
		result = best;
		
		s = result >> SCORE_SHIFT;
		if (s == CAPTURE_KING_MIN_SCORE + depth) {
			
			/* Direct hit, can't go on with this. */
			
			result = (result & SCORE_MASK) | IN_CHECK;

		} else if (s == -(CAPTURE_KING_MIN_SCORE + depth - 1)) {
			
			/*
				Hit on next depth = checkmate or stalemate. To determine which, we need to make a null move and play the
				opponent to see if he can take the king. No winner with stalemate so in this case we "zero" the score.
			*/
			
			if (canCaptureKing(switchColor(turnColor))) {
				result = (result & SCORE_MASK) | CHECKMATE;
			} else {
				result = (score << SCORE_SHIFT) | STALEMATE;
			}
		}
	}

	bestMove = (best & MOVE_MASK);
	if (bestMove != 0) {
		assert(isValidMove(bestMove));
		hashTable[hashIndex] = h | bestMove;
	}
	
	++evalCounter;
	if (evalCounter < 0) {
		--evalCounter; /* Prevent wrapping with extreme high evaluation counts. */
	}
	
	return result;
}

/*
	Can return: VALID_MOVE, CHECKMATE, STALEMATE, IN_CHECK or INVALID_MOVE.
*/
int executeMove(int move) {
	int tos[MAX_TARGET_SQUARES], from;

	assert(isValidMove(move));
	
	from = nextPiece[turnColor];
	while (from != turnColor) {
		int toCount, i;
		assert(board[from] > EMPTY && (board[from] & COLOR_MASK) == turnColor);
		toCount = listMoves(turnColor, from, tos);
		assert(toCount <= MAX_TARGET_SQUARES);
		for (i = 0; i < toCount; ++i) {
			int to = tos[i];
			if (move == (from | (to << TO_SHIFT))) {
				int oldPiece = board[from];
				int captured = board[to];
				int oldXCapture = xCapture;
				doMove(from, to, oldPiece, captured, oldXCapture);	
				
				switch (search(MIN_SCORE, MAX_SCORE, 1, 0, false) & MOVE_MASK) {
					default: return VALID_MOVE;
					case CHECKMATE: return CHECKMATE;
					case STALEMATE: return STALEMATE;
					case IN_CHECK: {
						undoMove(from, to, oldPiece, captured, oldXCapture);
						return IN_CHECK;
					}
				}
			}
		}
		from = nextPiece[from];
	}
		
	return INVALID_MOVE;
}

/*
	Can return: 16-bit move code or CHECKMATE or STALEMATE.
*/
int getComputerMove() {
	int d;
	bool printedThinking = false;
	int m = 0;
	evalCounter = 0;
	for (d = MIN_DEPTH; d <= MAX_DEPTH && evalCounter < minEvals; ++d) {
		if (evalCounter > 1000000) {
			if (!printedThinking) {
				fputs("Thinking...", stdout);
				printedThinking = true;
			}
			fputc('.', stdout);
			fflush(stdout);
		}
		m = search(MIN_SCORE, MAX_SCORE, d, 0, false);
	}
	if (printedThinking) {
		fputc('\n', stdout);
	}
	assert((m & MOVE_MASK) == CHECKMATE || (m & MOVE_MASK) == STALEMATE || isValidMove(m & MOVE_MASK));
	return m & MOVE_MASK;
}

enum {
	REDISPLAY_BOARD = -1
	, LET_COMPUTER_PLAY = -2
	, TAKE_BACK = -3
	, NEW_GAME = -4
	, QUIT = -5
	, SYNTAX_ERROR = -6
	, MATE = -7
	, SET_MAXIMUM_LEVEL = -93
	, SET_MINIMUM_LEVEL = -101
};

int getUserMove() {
	int result;
	int level;
	do {
		char s[1024];
		int l;
		result = SYNTAX_ERROR;
		putchar('>');
		fgets(s, 1023, stdin);
		l = strlen(s);
		if (l > 0 && s[l - 1] == '\n') s[l - 1] = 0;
		if (s[0] == 0) {
			result = REDISPLAY_BOARD;
		} else if (strcmp(s, "go") == 0) {
			result = LET_COMPUTER_PLAY;
		} else if (strcmp(s, "back") == 0) {
			result = TAKE_BACK;
		} else if (strcmp(s, "new") == 0) {
			result = NEW_GAME;
		} else if (strncmp(s, "level ", 6) == 0 && (level = atoi(s + 6)) >= 1 && level <= 9) {
			result = SET_MINIMUM_LEVEL + level - 1;
		} else if (strcmp(s, "quit") == 0) {
			result = QUIT;
		} else {
			int fromCol = (toupper(s[0]) - 'A');
			int fromRow = (s[1] - '1');
			int i = (s[2] == '-' ? 3 : 2);
			int toCol = (toupper(s[i]) - 'A');
			int toRow = (s[i + 1] - '1');
			if (fromCol >= 0 && fromCol < 8 && fromRow >= 0 && fromRow < 8
					&& toCol >= 0 && toCol < 8 && toRow >= 0 && toRow < 8) {
				result = makeMove(fromCol, fromRow, toCol, toRow);
			} else {
				fputs("Enter moves like 'd2d4' or:\n"
						"'level 1-9' to set computer level (default is 4).\n"
						"'go' to switch color and let the computer play.\n"
						"'back' to take back a step.\n"
						"'new' to start a new game.\n"
						"'quit' to exit.\n", stdout);
			}
		}
	} while (result == SYNTAX_ERROR);
	return result;
}

void printBoard(int from, int to) {
	int y;
	int lastI = -1;
	fputs("    A B C D E F G H\n", stdout);
	fputs("  +-----------------+\n", stdout);
	for (y = 0; y < 8; ++y) {
		char c;
		int x;
		putchar('8' - y);
		fputs(" |", stdout);
		for (x = 0; x < 8; ++x) {
			int i = (9 - y) * STRIDE + (x + 1);
			c = ' ';
			if (i == from || i == to) c = '(';
			if (lastI == from || lastI == to) c = (c == '(' ? ' ' : ')');
			putchar(c);
			c = PIECE_CHARS[board[i] & (PIECE_MASK | BLACK)];
			if (c == ' ' && ((x ^ y) & 1) == 1) c = '.';
			putchar(c);
			lastI = i;
		}
		c = ' ';
		if (lastI == from || lastI == to) c = ')';
		putchar(c);
		fputs("| ", stdout);
		putchar('8' - y);
		putchar('\n');
		lastI = -1;
	}
	fputs("  +-----------------+\n", stdout);
	fputs("    A B C D E F G H\n\n", stdout);
}

void registerMove(int move, int piece, int captured, int xCapture) {
	int i;
	if (historyCount >= HISTORY_SIZE) {
		int j;
		--historyCount;
		for (j = 0; j < historyCount * 4; ++j) {
			history[j] = history[j + 4];
		}
	}
	i = historyCount * 4;
	history[i + 0] = move;
	history[i + 1] = piece;
	history[i + 2] = captured;
	history[i + 3] = xCapture;
	++historyCount;
}

int takeBackMove() {
	if (historyCount > 0) {
		int i, move, from, to;
		--historyCount;
		i = historyCount * 4;
		move = history[i + 0];
		from = moveFrom(move);
		to = moveTo(move);
		undoMove(from, to, history[i + 1], history[i + 2], history[i + 3]);
		return move;
	} else {
		return 0;
	}
}

void setLevel(int level) {
	int i;
	minEvals = 100;
	for (i = 0; i < level; ++i) minEvals *= 5;
}

int main(int argc, const char* const argv[]) {
	initGlobals();
	restart();
	setLevel(3);

	printBoard(0, 0);
	{
		int lastPiece;
		bool isComputersTurn = false;
		int lastFrom = 0;
		int lastTo = 0;
		int lastMove = 0;
		int moveResult = 0;
		do {
			const char* action;

			if (lastMove >= 0) {
				fputs((turnColor == WHITE ? "White's turn.\n" : "Black's turn.\n"), stdout);
			}

			action = "Last move:";
			if (!isComputersTurn) {
				lastMove = getUserMove();
				switch (lastMove) {
					case REDISPLAY_BOARD: {
						printBoard(lastFrom, lastTo);
						break;
					}
					
					case LET_COMPUTER_PLAY: {
						isComputersTurn = true;
						break;
					}
				
					case TAKE_BACK: {					
						lastMove = takeBackMove();
						if (lastMove > 0) {
							lastFrom = moveFrom(lastMove);
							lastTo = moveTo(lastMove);
							lastMove = 0;
							action = "Took back move:";
						} else {
							fputs("No move to take back.\n", stdout);
							lastMove = -1;
						}
						break;
					}
				
					case NEW_GAME: {
						restart();
						lastMove = 0;
						lastFrom = 0;
						lastTo = 0;
						break;
					}
				
					default: {
						if (lastMove >= SET_MINIMUM_LEVEL && lastMove <= SET_MAXIMUM_LEVEL) {
							int level = lastMove - SET_MINIMUM_LEVEL;
							setLevel(level);
							fprintf(stdout, "Set computer level to %d.\n", level + 1);
							lastMove = -1;
						}
						break;
					}
				}
			}
			if (isComputersTurn) {
				lastMove = getComputerMove();
			}
			moveResult = 0;
			if (lastMove == CHECKMATE || lastMove == STALEMATE) {
				moveResult = lastMove;
				isComputersTurn = false;
				lastMove = -1;
			}
		
			if (lastMove > 0) {
				int lastCaptured, lastXCapture;
				lastFrom = moveFrom(lastMove);
				lastTo = moveTo(lastMove);
				lastPiece = board[lastFrom];
				lastCaptured = board[lastTo];
				lastXCapture = xCapture;

				moveResult = executeMove(lastMove);
				switch (moveResult) {
					case VALID_MOVE:
					case CHECKMATE:
					case STALEMATE: {
						registerMove(lastMove, lastPiece, lastCaptured, lastXCapture);
						if (moveResult == VALID_MOVE) {
							isComputersTurn = !isComputersTurn;
						} else {
							isComputersTurn = false;
						}
						break;
					}
				
					case IN_CHECK: {
						fputs("King in check.\n", stdout);
						lastMove = -1;
						break;
					}
				
					case INVALID_MOVE: {
						fputs("Invalid move.\n", stdout);
						lastMove = -1;
						break;
					}
				}
			}
		
			if (lastMove >= 0) {
				fputc('\n', stdout);
				printBoard(lastFrom, lastTo);
				if (lastFrom != 0 && lastTo != 0) {
					fprintf(stdout, "%s %s %c%c to %c%c.\n\n", action, PIECE_NAMES[lastPiece & PIECE_MASK]
							, ('A' + (lastFrom % STRIDE - 1)), ('1' + (lastFrom / STRIDE - 2))
							, ('A' + (lastTo % STRIDE - 1)), ('1' + (lastTo / STRIDE - 2)));
				}
			}
		
			if (moveResult == CHECKMATE) {
				fputs((turnColor == WHITE ? "Checkmate! Black wins.\n" : "Checkmate! White wins.\n"), stdout);
				lastMove = MATE;
			} else if (moveResult == STALEMATE) {
				fputs("Stalemate!\n", stdout);
				lastMove = MATE;
			}
	
		} while (lastMove != QUIT);
	}

    return 0;
}