# mhn_hotas
Linux USB device driver for Mitsubishi Hori/Namco Flightstick 2 HOTAS (Ace Combat 5)

It's developed/tested with Linux 6.10.12

USB ID is 06d3:0f10

## Caveat

I'm not a driver developer at all, and just stumbled through this. I've tested it for several hours and it seems rock solid, but looking at the source code will show the shrewd observer that I sincerely had no idea what I was doing. And no, I haven't cleaned it up at all because I don't think anybody would need/want a modern Linux driver for a 20-year-old controller (besides myself), but enh, here it is, just in case.

Used https://www.tamanegi.org/prog/hfsd/ as reference, and copied the data structs for control messages.

I didn't write this from scratch, I based it on "Driver for Phoenix RC Flight Controller Adapter" (pxrc) by Marcus Folkesson <marcus.folkesson@gmail.com>

## Notes

* M1/M2/M3 is detected but disabled, as I can't think of a proper way to implement this that doesn't break when binding.
* Neither dial knobs do anything. I suspect they're provided by a different vendor request. I have no use for them, so I haven't checked.
* A and B pressure sensitivity isn't respected. I just trigger "pressed" if they're pressed enough.
* D-PAD1 is split into 4 buttons, D-PAD3 is split into 3 buttons. D-PAD2 probably should be too, but ... we'll see.
* HAT is mapped to RX/RY for Elite reasons.
* HAT +PUSH button doesn't work, but that could be my flight stick? I left the button "on" in the driver in case it works for you.

Keep in mind that for Proton/Wine games Steam likes to emulate an Xbox 360 controller and override HID input devices in the name of compatibility, which will ruin a lot of functionality in this case.

## Usage

If you have the kernel headers installed for your Linux version (and build tools/compiler), just run the 'build.sh' script.

Then sudo insmod hori.ko

Test it with "jstest" or "jstest-gtk"

(Don't mind if the axiseses are a bit confused in the test app, binding them in game works just fine.)



