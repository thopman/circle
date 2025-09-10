//
// kernel.h
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/types.h>
#include "audio.hpp"
#include "midiconfig.h"
#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS || USE_USB_MIDI_GADGET
#include <circle/usb/usbhcidevice.h>
#endif
#ifdef USE_USB_MIDI_GADGET
#include <circle/usb/gadget/usbmidigadget.h>
#endif


enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

private:
	// do not change this order
	CActLED				m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer				m_Timer;
	CLogger				m_Logger;
	CI2CMaster			m_I2CMaster;
	CAudio				*m_pSound;
	#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
    CUSBHCIDevice       *m_pUSB;        // USB host controller
	#elif defined(USE_USB_MIDI_GADGET)
    CUSBMIDIGadget      *m_pUSB;        // USB MIDI gadget
	#endif

	#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    CSerialDevice       m_MidiSerial;   // Hardware UART for MIDI
	#endif
};

#endif
