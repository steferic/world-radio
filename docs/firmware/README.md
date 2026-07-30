# Architecture

This C++ project consists of a main script and a series of helper files which wrap certain hardware functions.

## Audio Helper

A wrapper around the MAX98357A audio amp IC and its circuitry, controlled by GPIO pins.

Fucntions:
- begin()
	- Method called to start radio service. Sets pinout, sets volume, plays audio from URL.
	- Params:
		- streamURL - An initial URL from which to fetch an audio stream.
		- bclk, lrc, & din - GPIO pin assignments going to the MAX98357A input pins of the same names.
	- Returns:
		- void
- loop()
	- Looping method to be called every time the main script loops. Calls Audio.loop() and also calculates some audio stream reconnect logic.
	- Params:
		- None
	- Returns:
		- void
- playURL()
	- Method to switch URLs during runtime without re-calling begin().
	- Params:
		- streamUrl - A new URL to an internet audio stream.
	- Returns:
		- void
- setVolume()
	- Method to change volume. Acceptable range is between 0 and 21, inclusive.
	- Params:
		- volume - An int between 0 and 21, inclusive, representing volume level. 0 = silent, 21 = max.
	- Returns:
		- void
- isRunning()
	- Method for determining whether the stream is currently active and playing audio. Wrapper around Audio.isRunning().
	- Params:
		- None
	- Returns:
		- `true` is audio is running, `false` if not.


## LCD Helper

## I/O Helper

## Wifi Helper