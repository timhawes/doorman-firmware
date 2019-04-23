#ifndef BUZZER_HPP
#define BUZZER_HPP

#include <Arduino.h>
#include <Ticker.h>

typedef struct { uint16_t frequency; uint16_t ms; } buzzer_note;
typedef buzzer_note *buzzer_playlist;

//buzzer_note trill[] = { {1, 20}, {0, 20}, {1, 20}, {0, 20}, {1, 20}, {0, 0} };
//buzzer_note ascending[] = { {1000, 250}, {1500, 250}, {2000, 250}, {0, 0} };

class Buzzer
{
private:
    Ticker ticker;
    int _pin;
    bool _modulate;
    double _default_frequency;
    buzzer_note *current_playlist;
    int playlist_position;
    int playlist_size;
    bool playlist_active = false;
    void stop();
    void next_note();
    bool active = false;
    const int max_note_length = 5000;
public:
    Buzzer(int pin, bool modulate=false, double default_frequency=2000);
    void beep(unsigned int ms);
    void beep(unsigned int ms, double frequency);
    void chirp();
    void click();
    void play(buzzer_note *playlist);
};

#endif
