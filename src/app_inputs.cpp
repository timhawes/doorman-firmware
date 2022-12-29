// SPDX-FileCopyrightText: 2019 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_inputs.h"

Inputs::Inputs(int door_pin, int exit_pin, int snib_pin)
{
  door_input = new Bounce();
  exit_input = new Bounce();
  snib_input = new Bounce();
  set_long_press_time(long_press_time);
  door_input->attach(door_pin, INPUT_PULLUP);
  exit_input->attach(exit_pin, INPUT_PULLUP);
  snib_input->attach(snib_pin, INPUT_PULLUP);
}

void Inputs::begin()
{

}

void Inputs::loop()
{
  door_input->update();
  if (door_close_high) {
    if (door_input->fell() && door_open_callback) door_open_callback();
    if (door_input->rose() && door_close_callback) door_close_callback();
  } else {
    if (door_input->rose() && door_open_callback) door_open_callback();
    if (door_input->fell() && door_close_callback) door_close_callback();
  }
  
  exit_input->update();
  if (exit_input->rose() && exit_release_callback) exit_release_callback();
  if (exit_input->fell() && exit_press_callback) exit_press_callback();
  if (exit_input->heldLow() && exit_longpress_callback) exit_longpress_callback();

  snib_input->update();
  if (snib_input->rose() && snib_release_callback) snib_release_callback();
  if (snib_input->fell() && snib_press_callback) snib_press_callback();
  if (snib_input->heldLow() && snib_longpress_callback) snib_longpress_callback();
}

void Inputs::set_long_press_time(int ms) {
  long_press_time = ms;
  door_input->holdTime(long_press_time);
  exit_input->holdTime(long_press_time);
  snib_input->holdTime(long_press_time);
}
