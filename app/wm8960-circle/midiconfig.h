#ifndef _MIDICONFIG_H
#define _MIDICONFIG_H

// MIDI Input Options - uncomment ONE of these:
//#define USE_SERIAL_MIDI         // Serial MIDI on GPIO14/15 at 31250 baud
#define USE_USB_MIDI_HOST       // USB MIDI host (connect USB MIDI keyboards)
//#define USE_USB_MIDI_GADGET     // USB MIDI gadget (Pi appears as USB MIDI device)

// Enable multiple MIDI inputs simultaneously:
//#define USE_ALL_MIDI_INPUTS     // Enable serial + USB host (not compatible with gadget)

#endif