# precokbd
USB keyboard controller for VTech Precomputer 1000

GPIOs are connected to 16 keyboard matrix from left to right.
The keyboard uses rubber resistive layer so scanning it is a little weird.

The keyboard is scanned twice, to better distinguish when multiple keys are pressed at once.

A little OLED display is connected to i2c for debugging and visualizing keyboard state.
