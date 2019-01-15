# Feeder

This project is an arduino-powered pet feeder.

Hardware setup:

 - Arduino Uno (ATMEGA328P)
 - DS3231 RTC module connected to i2c
 - Small piezo speaker (for warning buzzer)
 - 12v relay on output pin

See constructed feeder here:

https://photos.app.goo.gl/qWNCkPgJq5HooDmy9

The feeder is configured via serial over USB (9600 baud). Enter 'H' to list
available commands.

```
Help:
	T = set time
	C = read temperature
	A = add feeding
	D = delete feeding
	R = reset all configuration
	F = test feeder mechanism
 ```

Up to 9 daily feedings times can be scheduled using this interface. At each
scheduled time, the feeder will activate the relay mechanism for a configurable
duration to dispense food.
