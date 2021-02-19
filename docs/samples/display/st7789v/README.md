# ST7789V Display driver

make sure this patch is applied :
[https://github.com/zephyrproject-rtos/zephyr/pull/20570/files](https://github.com/zephyrproject-rtos/zephyr/pull/20570/files)

## Overview

This sample will draw some basic rectangles onto the display.
The rectangle colors and positions are chosen so that you can check the
orientation of the LCD and correct RGB bit order. The rectangles are drawn
in clockwise order, from top left corner: Red, Green, Blue, grey. The shade of
grey changes from black through to white. (if the grey looks too green or red
at any point then the LCD may be endian swapped).

Note: The display driver rotates the display so that the ‘natural’ LCD
orientation is effectively 270 degrees clockwise of the default display
controller orientation.

## Building and Running

## References


* [ST7789V datasheet](https://www.crystalfontz.com/controllers/Sitronix/ST7789V/)
