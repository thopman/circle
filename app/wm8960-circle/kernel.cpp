//
// kernel.cpp
//
#include "kernel.h"
#include <circle/util.h>
#include <assert.h>
#include <circle/machineinfo.h>
#ifdef USE_USB_MIDI_GADGET
#include <circle/usb/gadget/usbmidigadget.h>
#endif

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
    m_pUSB (new CUSBHCIDevice (&m_Interrupt, &m_Timer, TRUE)),  // TRUE: enable plug-and-play
	#elif defined(USE_USB_MIDI_GADGET)
    m_pUSB (new CUSBMIDIGadget (&m_Interrupt)),
	#endif
	#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    m_MidiSerial (&m_Interrupt, TRUE),  // TRUE for GPIO15 UART
	#endif
	m_I2CMaster (1, false, 0) // setup i2c with i2c controller 1, normal speed (100kHz) and standard pin mapping
{
	// m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
    boolean bOK = TRUE;

    if (bOK)
    {
        bOK = m_Serial.Initialize (115200);
    }

    if (bOK)
    {
        m_Logger.Initialize(&m_Serial);
    }

    if (bOK)
    {
        bOK = m_Interrupt.Initialize ();
    }

    if (bOK)
    {
        bOK = m_Timer.Initialize ();
    }

#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS || USE_USB_MIDI_GADGET
    // Initialize USB (host or gadget)
    if (bOK)
    {
        assert(m_pUSB);
        bOK = m_pUSB->Initialize();
        
        if (bOK) {
#ifdef USE_USB_MIDI_GADGET
            m_Logger.Write(FromKernel, LogNotice, "USB MIDI gadget initialized");
#else
            m_Logger.Write(FromKernel, LogNotice, "USB host initialized for MIDI");
#endif
        } else {
            m_Logger.Write(FromKernel, LogError, "Failed to initialize USB");
        }
    }
#endif

#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    // Initialize Serial MIDI
    if (bOK)
    {
        bOK = m_MidiSerial.Initialize(31250);  // MIDI baud rate
        if (bOK) {
            m_Logger.Write(FromKernel, LogNotice, "Serial MIDI initialized on GPIO14/15");
        } else {
            m_Logger.Write(FromKernel, LogError, "Failed to initialize Serial MIDI");
        }
    }
#endif

    if (bOK)
    {
        bOK = m_I2CMaster.Initialize ();
    }

    m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
    m_Timer.MsDelay(1000);

    m_pSound = new CAudio (&m_Interrupt, &m_I2CMaster);
    
#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    // Set serial MIDI reference in audio class
    m_pSound->SetMidiSerial(&m_MidiSerial);
#endif

    m_Logger.Write (FromKernel, LogNotice, "CAudio initialized with MIDI support");

    return bOK;
}

TShutdownMode CKernel::Run (void)
{
    if (!m_pSound->Start ()){
        m_Logger.Write (FromKernel, LogPanic, "Cannot start sound device");
    } else {
        m_Logger.Write (FromKernel, LogNotice, "Started sound device");
    }

    while(1){
#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS || USE_USB_MIDI_GADGET
        // Update USB plug-and-play
        assert(m_pUSB);
        boolean bUpdated = m_pUSB->UpdatePlugAndPlay();
#else
        boolean bUpdated = FALSE;
#endif
        
        // Process MIDI messages
        m_pSound->ProcessMidi(bUpdated);
        
        // Short delay to prevent CPU overload but keep MIDI responsive
        m_Timer.MsDelay(1);  // 1ms delay = ~1000Hz MIDI polling rate
        
        // Optional: Add periodic status logging (every 10 seconds)
        static unsigned nCount = 0;
        if (++nCount >= 10000) {  // 10000 * 1ms = 10 seconds
            // m_Logger.Write (FromKernel, LogNotice, "System running, MIDI active");
            nCount = 0;
        }
    }

    return ShutdownHalt;
}