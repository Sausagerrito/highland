# Microcontroller Programming
The Teensy4.1 is the most compatible with the Arduino IDE, the bootloader it uses to flash the code to the MCU doesn't like to work with pretty much anything else.

To interpret commands from the app, we need a communications protocol, that takes in serial commands and reads them to set state.

The best option is an ANSII key-value string, it would look like this

"CMD=RUN TMP=1200 TIM=2400\n"

So the MCU would read until the new line character, and set each value accordingly.


