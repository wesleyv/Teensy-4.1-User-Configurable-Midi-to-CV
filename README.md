This project is loosely based on Elkayem's Teensy midi to CV Teensy project. 
This project contains code and a PCB Layout/Schematic for a Midi to CV converter with a standard Eurorack Power connector, for use in a modular synth (+12/-12 volt supply).
NOTE: Unless you have lots of extra headroom on your power supply, I recommend reducing the clock speed of the Teensy 4.1 to 24mHz in order to reduce temperatures and current draw.
This project is very DIY and should only be attempted if you have decent troubleshooting skills. I implemented all changes/corrections to my prototype into the current version uploaded here. 
I have not, however, tested the newest version of the board. I plan to implement further refinements in the future. 
This version of the project has 8 CV outputs that can be freely assigned to keyboard tracking or any MIDI parameter, as well as gate/trigger. There are currently also 4 outputs for gate/trigger signals only.
The board has a 5 pin midi input, as well as USB midi capabilites. Both MIDI sources are merged. There is currently no MIDI through implemented. 
There is also a single knob, 4 channel slew rate limiter integrated into the project to allow for keyboard portamento, as well as switches for per-channel bypass of the slew circuit. 
A video description of the device's functions can be found here:

The software also contains a MIDI monitor to assist with troubleshooting, as well as a MIDI learn function for all controls, so that adjusting a parameter on your MIDI controller will automatically assign that parameter to the selected output.
Most components are bog standard but you will need a special potentiometer for the 4 channel slew rate limiter:
https://www.mouser.com/ProductDetail/Bourns/PTD904-2015K-C503?qs=%2FxQVPCMPNzjGk%2Fu1%252BtkJhQ%3D%3D&countrycode=US&currencycode=USD
And making an easily accessed USB port will be much easier with a panel-mount-to-plug-in solution such as this:
https://www.amazon.com/dp/B07FL3MKLK

I plan to further iterate on this project by adding trimmable slew rate limiting to to each channel to smooth out steppy midi parameter voltages. I would also like to clean up and further optimize the code, 
as well as implementing a better solution for single-wire keyboard tracking with pitchbend to improve pitch bend resolution. I also plan to create a layout with monolithic construction instead of offboard wiring.
