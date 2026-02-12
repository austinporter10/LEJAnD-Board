/*
 * mcu-max serial port example with physical board sensor simulation
 *
 * (C) 2022-2024 Gissio
 *
 * License: MIT
 */
#include <mcu-max.h>

// Modify these values to increase the AI strength:
#define MCUMAX_NODE_MAX 1000
#define MCUMAX_DEPTH_MAX 3
#define GAME_VALID_MOVES_NUM_MAX 181

// Board state tracking
uint64_t previous_board_state = 0;
uint64_t current_board_state = 0;
bool piece_lifted = false;
int lifted_square = -1;
bool ignore_next_change = false;  // Flag to ignore AI's simulated move

// Simulation mode
bool simulation_mode = true;  // Set to false when using real sensors
// Starting position: 0 = piece present, 1 = empty
// Each file (column): a8,a7,a6,a5,a4,a3,a2,a1 then b8,b7... etc.
// Ranks 8,7 have pieces (00), ranks 6-3 empty (1111), ranks 2,1 have pieces (00)
// Per file: 00111100 = 0x3C
uint64_t simulated_board_state = 0x3C3C3C3C3C3C3C3CULL;  // Starting position

// Convert algebraic notation square to bit index (0-63)
int squareToIndex(const char* square) {
  int file = square[0] - 'a';  // 'a'=0, 'b'=1, ..., 'h'=7
  int rank = square[1] - '1';  // '1'=0, '2'=1, ..., '8'=7
  
  // Your format: columns are a8-a1, b8-b1, etc.
  int columnStart = file * 8;
  int bitIndex = columnStart + (7 - rank);  // 7-rank because a8 is first
  
  return bitIndex;
}

// Convert bit index back to algebraic notation
void indexToSquare(int index, char* square) {
  int file = index / 8;
  int rank = 7 - (index % 8);
  
  square[0] = 'a' + file;
  square[1] = '1' + rank;
  square[2] = '\0';
}

// Parse move string like "c2c4" and return from/to indices
void parseMove(const char* move, int* fromIndex, int* toIndex) {
  char fromSquare[3];
  char toSquare[3];
  
  // Extract from square (first 2 chars)
  fromSquare[0] = move[0];
  fromSquare[1] = move[1];
  fromSquare[2] = '\0';
  
  // Extract to square (last 2 chars)
  toSquare[0] = move[2];
  toSquare[1] = move[3];
  toSquare[2] = '\0';
  
  *fromIndex = squareToIndex(fromSquare);
  *toIndex = squareToIndex(toSquare);
}

// Simulate a physical move on the board
void simulatePhysicalMove(const char* move_str) {
  int from_idx, to_idx;
  parseMove(move_str, &from_idx, &to_idx);
  
  Serial.print("Simulating move: ");
  Serial.println(move_str);
  
  // Do the move in one step (both lift and place together)
  // This avoids the intermediate state issue
  simulated_board_state |= (1ULL << from_idx);   // Remove from source
  simulated_board_state &= ~(1ULL << to_idx);    // Add to destination
  
  Serial.println("Move completed");
}

// Undo a simulated move (restore previous state)
void undoSimulatedMove(uint64_t restore_state) {
  simulated_board_state = restore_state;
  Serial.println("Board state restored to previous position");
}

// Detect move from board state changes
// Returns true if a complete move is detected
bool detectMove(uint64_t prev_state, uint64_t curr_state, char* move_str) {
  uint64_t changed = prev_state ^ curr_state;  // XOR to find differences
  
  int changed_squares[64];
  int num_changed = 0;
  
  // Find all changed squares
  for (int i = 0; i < 64; i++) {
    if (changed & (1ULL << i)) {
      changed_squares[num_changed++] = i;
    }
  }
  
  // Simple move or capture: exactly 2 squares changed
  if (num_changed == 2) {
    int from_idx = -1;
    int to_idx = -1;
    
    // Determine which is from (piece removed: 0->1) and to (piece added: 1->0)
    for (int i = 0; i < 2; i++) {
      int sq = changed_squares[i];
      bool was_occupied = !(prev_state & (1ULL << sq));  // 0 = piece present
      bool is_occupied = !(curr_state & (1ULL << sq));
      
      if (was_occupied && !is_occupied) {
        from_idx = sq;  // Piece was here, now gone
      } else if (!was_occupied && is_occupied) {
        to_idx = sq;    // Piece wasn't here, now is
      }
    }
    
    if (from_idx >= 0 && to_idx >= 0) {
      char from_sq[3], to_sq[3];
      indexToSquare(from_idx, from_sq);
      indexToSquare(to_idx, to_sq);
      sprintf(move_str, "%s%s", from_sq, to_sq);
      piece_lifted = false;  // Reset lift tracking
      lifted_square = -1;
      return true;
    }
  }
  
  return false;
}

void print_board() {
  const char *symbols = ".PPNKBRQ.ppnkbrq";
  Serial.println("");
  Serial.println("  +-----------------+");
  for (uint32_t y = 0; y < 8; y++) {
    Serial.print(8 - y);
    Serial.print(" | ");
    for (uint32_t x = 0; x < 8; x++) {
      Serial.print(symbols[mcumax_get_piece(0x10 * y + x)]);
      Serial.print(' ');
    }
    Serial.println("|");
  }
  Serial.println("  +-----------------+");
  Serial.println("    a b c d e f g h");
  Serial.println("");
}

void print_binary_64(uint64_t value) {
  Serial.print("Binary: 0b");
  for (int i = 63; i >= 0; i--) {
    Serial.print((value & (1ULL << i)) ? '1' : '0');
    if (i % 8 == 0 && i > 0) {
      Serial.print('_');  // Add separator every 8 bits for readability
    }
  }
  Serial.println("");
  Serial.print("Hex: 0x");
  // Print in uppercase hex
  char hex[17];
  sprintf(hex, "%08lX%08lX", (unsigned long)(value >> 32), (unsigned long)(value & 0xFFFFFFFF));
  Serial.println(hex);
}

void print_sensor_board(uint64_t board_state) {
  Serial.println("Sensor Board State (0=piece, 1=empty):");
  Serial.println("  +-----------------+");
  for (int rank = 7; rank >= 0; rank--) {  // Start from rank 8 (index 7) down to rank 1 (index 0)
    Serial.print(rank + 1);
    Serial.print(" | ");
    for (int file = 0; file < 8; file++) {  // a through h
      // Build the square name
      char square[3];
      square[0] = 'a' + file;
      square[1] = '1' + rank;
      square[2] = '\0';
      
      int bit_index = squareToIndex(square);
      bool is_empty = (board_state & (1ULL << bit_index)) != 0;
      Serial.print(is_empty ? '.' : 'X');
      Serial.print(' ');
    }
    Serial.println("|");
  }
  Serial.println("  +-----------------+");
  Serial.println("    a b c d e f g h");
  Serial.println("");
  print_binary_64(board_state);
  Serial.println("");
}

void print_square(mcumax_square square) {
  Serial.print((char)('a' + ((square & 0x07) >> 0)));
  Serial.print((char)('1' + 7 - ((square & 0x70) >> 4)));
}

void print_move(mcumax_move move) {
  if ((move.from == MCUMAX_SQUARE_INVALID) || (move.to == MCUMAX_SQUARE_INVALID))
    Serial.print("(none)");
  else {
    print_square(move.from);
    print_square(move.to);
  }
}

// Convert mcumax_move to string format "e2e4"
void mcumaxMoveToString(mcumax_move move, char* move_str) {
  char from_sq[3], to_sq[3];
  
  // Convert from square
  from_sq[0] = 'a' + (move.from & 0x07);
  from_sq[1] = '1' + 7 - ((move.from & 0x70) >> 4);
  from_sq[2] = '\0';
  
  // Convert to square
  to_sq[0] = 'a' + (move.to & 0x07);
  to_sq[1] = '1' + 7 - ((move.to & 0x70) >> 4);
  to_sq[2] = '\0';
  
  sprintf(move_str, "%s%s", from_sq, to_sq);
}

mcumax_square get_square(const char *s) {
  mcumax_square rank = s[0] - 'a';
  if (rank > 7)
    return MCUMAX_SQUARE_INVALID;
  mcumax_square file = '8' - s[1];
  if (file > 7)
    return MCUMAX_SQUARE_INVALID;
  return 0x10 * file + rank;
}

// Function to read board state from sensors (you'll implement this)
uint64_t readBoardSensors() {
  if (simulation_mode) {
    return simulated_board_state;
  }
  
  // TODO: Replace this with actual sensor reading code
  // Return a 64-bit number where 0 = piece present, 1 = empty
  // Organized as: a8-a1 (bits 0-7), b8-b1 (bits 8-15), etc.
  return 0;
}

char serial_input[5];

void init_serial_input() {
  serial_input[0] = '\0';
}

bool get_serial_input() {
  if (Serial.available()) {
    char s[2];
    s[0] = Serial.read();
    s[1] = '\0';
    if (s[0] == '\n')
      return true;
    if (s[0] == '\b') {
      int n = strlen(serial_input);
      if (n)
        serial_input[n - 1] = '\0';
    }
    if (s[0] >= ' ') {
      if (strlen(serial_input) < 4)
        strcat(serial_input, s);
      Serial.print(s[0]);
    }
  }
  return false;
}

// Validate a move before simulating it
bool validateAndSimulateMove(const char* move_str) {
  mcumax_move move = (mcumax_move){
    get_square(move_str),
    get_square(move_str + 2),
  };
  
  // Check if it's a valid move
  mcumax_move valid_moves[GAME_VALID_MOVES_NUM_MAX];
  uint32_t valid_moves_num = mcumax_search_valid_moves(valid_moves, GAME_VALID_MOVES_NUM_MAX);
  bool is_valid_move = false;
  for (uint32_t i = 0; i < valid_moves_num; i++) {
    if ((valid_moves[i].from == move.from) && (valid_moves[i].to == move.to)) {
      is_valid_move = true;
      break;
    }
  }
  
  if (!is_valid_move) {
    Serial.println("INVALID MOVE! That move is not legal in the current position.");
    return false;
  }
  
  // Simulate the move
  simulatePhysicalMove(move_str);
  return true;
}

void setup() {
  Serial.begin(9600);
  init_serial_input();
  mcumax_init();
  
  Serial.println("mcu-max physical board example (SIMULATION MODE)");
  Serial.println("------------------------------------------------");
  Serial.println("");
  Serial.println("Commands:");
  Serial.println("  - Enter move like 'e2e4' to simulate physical move");
  Serial.println("  - Type 'show' to see sensor board state");
  Serial.println("");
  
  // Initialize board state (all pieces in starting position)
  // 0 = piece present, 1 = empty
  // Each byte represents one file: bit 0 = a8, bit 1 = a7, ... bit 7 = a1
  // 0x3C = 00111100 = ranks 8,7 occupied, 6-3 empty, 2,1 occupied
  simulated_board_state = 0x3C3C3C3C3C3C3C3CULL;
  
  previous_board_state = readBoardSensors();
  current_board_state = previous_board_state;
  
  print_board();
  print_sensor_board(current_board_state);
  Serial.print("Ready> ");
}

void loop() {
  // Read current sensor state FIRST
  uint64_t new_board_state = readBoardSensors();
  
  // Check if board state changed
  if (new_board_state != current_board_state) {
    uint64_t state_before_move = current_board_state;  // Save state before processing move
    previous_board_state = current_board_state;
    current_board_state = new_board_state;
    
    // Check if we should ignore this change (AI just moved)
    if (ignore_next_change) {
      ignore_next_change = false;
      // Print sensor state after AI move
      print_sensor_board(current_board_state);
      if (simulation_mode) {
        Serial.print("Ready> ");
      }
    } else {
      char move_str[5];
      if (detectMove(previous_board_state, current_board_state, move_str)) {
        Serial.println("");
        Serial.print("Move detected: ");
        Serial.println(move_str);
        
        mcumax_move move = (mcumax_move){
          get_square(move_str),
          get_square(move_str + 2),
        };
        
        mcumax_move valid_moves[GAME_VALID_MOVES_NUM_MAX];
        uint32_t valid_moves_num = mcumax_search_valid_moves(valid_moves, GAME_VALID_MOVES_NUM_MAX);
        bool is_valid_move = false;
        for (uint32_t i = 0; i < valid_moves_num; i++)
          if ((valid_moves[i].from == move.from) &&
            (valid_moves[i].to == move.to))
            is_valid_move = true;
        
        if (!is_valid_move || !mcumax_play_move(move)) {
          Serial.println("INVALID MOVE! Please place the pieces back.");
          
          // Reset sensor state to what it was before the invalid move
          if (simulation_mode) {
            undoSimulatedMove(state_before_move);
            current_board_state = state_before_move;
          }
          
          print_sensor_board(current_board_state);
          
          if (simulation_mode) {
            Serial.print("Ready> ");
          }
        } else {
          // Print sensor state after player move
          print_sensor_board(current_board_state);
          
          Serial.println("Calculating AI response...");
          mcumax_move ai_move = mcumax_search_best_move(MCUMAX_NODE_MAX, MCUMAX_DEPTH_MAX);
          if (ai_move.from == MCUMAX_SQUARE_INVALID) {
            Serial.println("Game over.");
            print_board();
            if (simulation_mode) {
              Serial.print("Ready> ");
            }
          } else if (mcumax_play_move(ai_move)) {
            Serial.print("AI moves: ");
            print_move(ai_move);
            Serial.println("");
            
            // In simulation mode, automatically make the AI move
            if (simulation_mode) {
              char ai_move_str[5];
              mcumaxMoveToString(ai_move, ai_move_str);
              
              Serial.println("Simulating AI move...");
              ignore_next_change = true;  // Ignore the change we're about to make
              delay(1000);
              simulatePhysicalMove(ai_move_str);
            } else {
              Serial.println("Please make the AI's move on the board.");
            }
            
            print_board();
            // Sensor state will be printed when the change is detected
          }
        }
      }
    }
  }
  
  // In simulation mode, wait for serial input
  if (simulation_mode) {
    if (!get_serial_input())
      return;
    
    Serial.println("");
    
    // Check for special commands
    if (strcmp(serial_input, "show") == 0) {
      print_sensor_board(simulated_board_state);
      init_serial_input();
      Serial.print("Ready> ");
      return;
    }
    
    // Validate and simulate the physical move
    if (strlen(serial_input) == 4) {
      if (!validateAndSimulateMove(serial_input)) {
        // Invalid move, don't proceed
        init_serial_input();
        Serial.print("Ready> ");
        return;
      }
    }
    
    init_serial_input();
  }
  
  if (!simulation_mode) {
    delay(100);  // Poll sensors every 100ms in real mode
  }
}