#ifndef APP_INPUTS_H
#define APP_INPUTS_H

#include <Bounce2.h>

typedef void (*input_callback_t)();

class Inputs
{
private:
  Bounce *door_input;
  Bounce *exit_input;
  Bounce *snib_input;
  int long_press_time = 1000;
public:
  Inputs(int door_pin, int exit_pin, int snib_pin);
  void begin();
  void loop();
  void set_long_press_time(int ms);
  bool door_close_high = false;
  bool exit_active_high = false;
  bool snib_active_high = false;
  input_callback_t prog_callback;
  input_callback_t door_open_callback;
  input_callback_t door_close_callback;
  input_callback_t exit_press_callback;
  input_callback_t exit_longpress_callback;
  input_callback_t exit_release_callback;
  input_callback_t snib_press_callback;
  input_callback_t snib_longpress_callback;
  input_callback_t snib_release_callback;
};

#endif
