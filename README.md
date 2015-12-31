# scope-term
Oscilloscope raster graphics.

# DESCRIPTION

Uses a computer's VGA port as three 8-bit DACs to control the axes and
intensity of an oscilloscope operating in X-Y mode.

# USAGE

A normal female VGA port looks like this:

    (h) ( ) (B) (G) (R)
      (v) ( ) (b) (g) (r)
    ( ) (V) (H) ( ) ( )

(Capital letters are sources, lowercase letters are returns.)

We will use the red channel to control the Y axis, the green channel
to control the X axis, and the blue channel to control the beam
intensity. The oscilloscope connections are as follows:

`(R)` to oscilloscope channel 2 (probe)
`(G)` to oscilloscope channel 1 (probe)
`(g)` to oscilloscope channel 1 (ground)
`(B)` to oscilloscope intensity modulation (+)
`(b)` to oscilloscope intensity modulation (-)

The intensity modulation input is often found on the back of the
oscilloscope. It wants TTL signals, so I'd recommend using a straight
BNC cable to connect this instead of a probe.

On the software side, we need to set up a custom display mode on the
VGA port. On Linux, this is done with

    xrandr --newmode "256x126" 2 256 257 257 260 126 127 127 135 +hsync +vsync
    xrandr --addmode <your VGA port> 256x126
    xrandr --output <your VGA port> --right-of <your actual screen> --mode 256x126

Don't connect certain old CRT monitors to the VGA port after you do
this. Best case, it won't work. However, these settings are cleared
after a X restart, so it's not permanent.

Then, just run `./sdis2` (it's configured to use your second
"monitor") and enjoy!

# BUGS

The VT100 emulation is terrible. Don't try to run anything that isn't
irssi. `emacs` doesn't display right, `bash` and similar command line
programs don't echo properly, etc.

Pull requests improving the terminal emulation are highly appreciated.

