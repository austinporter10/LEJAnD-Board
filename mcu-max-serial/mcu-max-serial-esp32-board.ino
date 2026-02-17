/*
 * mcu-max serial + ESP32 DevKit automatic chessboard bridge
 *
 * Based on mcu-max serial example
 * (C) 2022-2024 Gissio
 * License: MIT
 */

#include <mcu-max.h>

// Engine strength (tune as needed for ESP32 performance)
#define MCUMAX_NODE_MAX 2500
#define MCUMAX_DEPTH_MAX 4

#define GAME_VALID_MOVES_NUM_MAX 181

// -----------------------------
// ESP32 DevKit pin mapping
// -----------------------------
// Shift register (74HC165)
static const int PIN_SHIFT_SERIAL_OUT = 21;
static const int PIN_SHIFT_LOAD = 0;
static const int PIN_SHIFT_CLOCK = 2;

// X motor
static const int PIN_X_DIR = 16;
static const int PIN_X_STEP = 4;

// Y motor
static const int PIN_Y_DIR = 17;
static const int PIN_Y_STEP = 5;

// Magnet MOSFET gate
static const int PIN_MAGNET = 18;

// UI button
static const int PIN_BUTTON = 19;

// Limit switches (external pull-up)
static const int PIN_X_LIMIT = 36;
static const int PIN_Y_LIMIT = 39;

// Screen
static const int PIN_SCREEN_SDA = 32;
static const int PIN_SCREEN_SCL = 33;
static const int PIN_SCREEN_DC = 25;
static const int PIN_SCREEN_RST = 26;

// Rotary encoder
static const int PIN_ENCODER_A = 27;
static const int PIN_ENCODER_B = 14;

// LEDs intentionally disabled (hardware not populated)
// static const int PIN_LED_RED = 22;
// static const int PIN_LED_GREEN = 23;

// Motion tuning (adjust after calibration)
static const int32_t X_STEPS_PER_SQUARE = 200;
static const int32_t Y_STEPS_PER_SQUARE = 200;
static const int32_t HOMING_BACKOFF_STEPS = 120;
static const int STEP_PULSE_US = 700;
static const int HOME_FEED_DELAY_US = 900;

// Limit polarity: with pull-up wiring, pressed switch is typically LOW.
static const int LIMIT_ACTIVE_STATE = LOW;

char serial_input[5];
int32_t current_x_steps = 0;
int32_t current_y_steps = 0;

void pulse_step(int step_pin, int delay_us) {
  digitalWrite(step_pin, HIGH);
  delayMicroseconds(delay_us);
  digitalWrite(step_pin, LOW);
  delayMicroseconds(delay_us);
}

void move_axis_relative(int dir_pin, int step_pin, int32_t delta_steps, int pulse_delay_us) {
  if (delta_steps == 0) {
    return;
  }

  bool dir_forward = delta_steps > 0;
  digitalWrite(dir_pin, dir_forward ? HIGH : LOW);

  int32_t steps = abs(delta_steps);
  for (int32_t i = 0; i < steps; i++) {
    pulse_step(step_pin, pulse_delay_us);
  }
}

void move_xy_to_steps(int32_t target_x, int32_t target_y) {
  move_axis_relative(PIN_X_DIR, PIN_X_STEP, target_x - current_x_steps, STEP_PULSE_US);
  current_x_steps = target_x;

  move_axis_relative(PIN_Y_DIR, PIN_Y_STEP, target_y - current_y_steps, STEP_PULSE_US);
  current_y_steps = target_y;
}

void home_axis(int dir_pin, int step_pin, int limit_pin) {
  digitalWrite(dir_pin, LOW);

  uint32_t guard = 0;
  while ((digitalRead(limit_pin) != LIMIT_ACTIVE_STATE) && (guard < 50000)) {
    pulse_step(step_pin, HOME_FEED_DELAY_US);
    guard++;
  }

  move_axis_relative(dir_pin, step_pin, HOMING_BACKOFF_STEPS, HOME_FEED_DELAY_US);

  digitalWrite(dir_pin, LOW);
  guard = 0;
  while ((digitalRead(limit_pin) != LIMIT_ACTIVE_STATE) && (guard < 50000)) {
    pulse_step(step_pin, HOME_FEED_DELAY_US + 250);
    guard++;
  }
}

void home_gantry() {
  home_axis(PIN_X_DIR, PIN_X_STEP, PIN_X_LIMIT);
  home_axis(PIN_Y_DIR, PIN_Y_STEP, PIN_Y_LIMIT);
  current_x_steps = 0;
  current_y_steps = 0;
}

void set_magnet(bool enabled) {
  digitalWrite(PIN_MAGNET, enabled ? HIGH : LOW);
}

void square_to_steps(mcumax_square square, int32_t *x_steps, int32_t *y_steps) {
  int32_t file = (square & 0x0F);
  int32_t board_row = ((square & 0x70) >> 4);

  // Convert 0x88 row (top=0) to rank index from White side (rank1=0).
  int32_t rank_from_white = 7 - board_row;

  *x_steps = file * X_STEPS_PER_SQUARE;
  *y_steps = rank_from_white * Y_STEPS_PER_SQUARE;
}

void execute_bot_move(mcumax_move move) {
  if ((move.from == MCUMAX_SQUARE_INVALID) || (move.to == MCUMAX_SQUARE_INVALID)) {
    return;
  }

  int32_t from_x, from_y, to_x, to_y;
  square_to_steps(move.from, &from_x, &from_y);
  square_to_steps(move.to, &to_x, &to_y);

  move_xy_to_steps(from_x, from_y);
  set_magnet(true);
  delay(50);

  move_xy_to_steps(to_x, to_y);
  set_magnet(false);
  delay(50);

  // Capture/special-move handling can be added here if needed.
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
  Serial.print("Move: ");
}

void print_square(mcumax_square square) {
  Serial.print((char)('a' + ((square & 0x07) >> 0)));
  Serial.print((char)('1' + 7 - ((square & 0x70) >> 4)));
}

void print_move(mcumax_move move) {
  if ((move.from == MCUMAX_SQUARE_INVALID) || (move.to == MCUMAX_SQUARE_INVALID)) {
    Serial.print("(none)");
  } else {
    print_square(move.from);
    print_square(move.to);
  }
}

mcumax_square get_square(char *s) {
  mcumax_square rank = s[0] - 'a';
  if (rank > 7) {
    return MCUMAX_SQUARE_INVALID;
  }

  mcumax_square file = '8' - s[1];
  if (file > 7) {
    return MCUMAX_SQUARE_INVALID;
  }

  return 0x10 * file + rank;
}

void init_serial_input() {
  serial_input[0] = '\0';
}

bool get_serial_input() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if ((c == '\n') || (c == '\r')) {
      return strlen(serial_input) == 4;
    }

    if ((c == '\b') || (c == 127)) {
      size_t n = strlen(serial_input);
      if (n > 0) {
        serial_input[n - 1] = '\0';
      }
      continue;
    }

    if ((c >= ' ') && (strlen(serial_input) < 4)) {
      serial_input[strlen(serial_input)] = c;
      serial_input[strlen(serial_input) + 1] = '\0';
      Serial.print(c);
    }
  }

  return false;
}

void setup() {
  // pinMode(LED_BUILTIN, OUTPUT);

  pinMode(PIN_SHIFT_LOAD, OUTPUT);
  pinMode(PIN_SHIFT_CLOCK, OUTPUT);
  pinMode(PIN_SHIFT_SERIAL_OUT, INPUT);

  pinMode(PIN_X_DIR, OUTPUT);
  pinMode(PIN_X_STEP, OUTPUT);
  pinMode(PIN_Y_DIR, OUTPUT);
  pinMode(PIN_Y_STEP, OUTPUT);

  pinMode(PIN_MAGNET, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  pinMode(PIN_X_LIMIT, INPUT);
  pinMode(PIN_Y_LIMIT, INPUT);

  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);

  // pinMode(PIN_LED_RED, OUTPUT);
  // pinMode(PIN_LED_GREEN, OUTPUT);

  set_magnet(false);

  Serial.begin(115200);
  delay(200);

  init_serial_input();
  mcumax_init();

  Serial.println("mcu-max ESP32 automatic chessboard");
  Serial.println("---------------------------------");
  Serial.println("Enter player moves as [from][to], e.g. e2e4");
  Serial.println("Homing gantry...");

  home_gantry();

  Serial.println("Ready.");
  print_board();
}

void loop() {
  if (!get_serial_input()) {
    return;
  }

  Serial.println("");

  mcumax_move player_move = (mcumax_move){
    get_square(serial_input + 0),
    get_square(serial_input + 2),
  };

  mcumax_move valid_moves[GAME_VALID_MOVES_NUM_MAX];
  uint32_t valid_moves_num = mcumax_search_valid_moves(valid_moves, GAME_VALID_MOVES_NUM_MAX);
  bool is_valid_move = false;

  for (uint32_t i = 0; i < valid_moves_num; i++) {
    if ((valid_moves[i].from == player_move.from) &&
        (valid_moves[i].to == player_move.to)) {
      is_valid_move = true;
      break;
    }
  }

  if (!is_valid_move || !mcumax_play_move(player_move)) {
    Serial.println("Invalid move.");
    init_serial_input();
    print_board();
    return;
  }

  mcumax_move bot_move = mcumax_search_best_move(MCUMAX_NODE_MAX, MCUMAX_DEPTH_MAX);
  if (bot_move.from == MCUMAX_SQUARE_INVALID) {
    Serial.println("Game over.");
  } else if (mcumax_play_move(bot_move)) {
    Serial.print("Opponent moves: ");
    print_move(bot_move);
    Serial.println("");

    execute_bot_move(bot_move);
  }

  init_serial_input();
  print_board();
}
