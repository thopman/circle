#ifndef _audio_hpp_
#define _audio_hpp_


#include "midiconfig.h"
#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
#include <circle/serial.h>
#endif
#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
#include <circle/usb/usbmidi.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/devicenameservice.h>
#endif

#include <circle/interrupt.h>
#include <circle/i2cmaster.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <cstdint>

class circleFaustDSP;

class CAudio : public CI2SSoundBaseDevice
{
private:
    static const unsigned AUDIO_BLOCK_SIZE = 256;
    static const unsigned NUM_FRAMES = AUDIO_BLOCK_SIZE / 2;
    static const unsigned SAMPLE_RATE = 48000;
    static const unsigned I2C_ADDRESS = 0x1A;
    
    circleFaustDSP *aCircleFaustDSP;
    
    // Double buffering for I2S data (raw 32-bit samples)
    u32 inputBuffer_A[AUDIO_BLOCK_SIZE];
    u32 inputBuffer_B[AUDIO_BLOCK_SIZE];
    u32 outputBuffer_A[AUDIO_BLOCK_SIZE];
    u32 outputBuffer_B[AUDIO_BLOCK_SIZE];
    
    // Current buffer pointers
    u32 *currentInputBuffer;
    u32 *currentOutputBuffer;
    bool useBufferA;
    
    // Float buffers for Faust processing (separate L/R channels)
    float inputLeft[NUM_FRAMES];
    float inputRight[NUM_FRAMES];
    float outputLeft[NUM_FRAMES];
    float outputRight[NUM_FRAMES];
    
    // Private helper functions (implementations in .cpp)
    void convertAndDeinterleave();
    void convertAndInterleave();

    // MIDI handling
    #ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    CSerialDevice   *m_pMidiSerial;      // Pointer to serial MIDI device
    boolean         m_bUseSerial;        // Serial MIDI available
    unsigned        m_nSerialState;      // Serial MIDI parsing state
    u8              m_SerialMessage[3];  // Serial MIDI message buffer
    #endif

    #ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
    CUSBMIDIDevice     * volatile m_pMIDIDevice;   // USB MIDI device
    CUSBKeyboardDevice * volatile m_pKeyboard;     // USB keyboard (optional)
    #endif

    // Common MIDI state
    u8              m_nMidiType;         // Current MIDI message type
    u8              m_nMidiChannel;      // Current MIDI channel
    u8              m_nMidiData1;        // First data byte
    u8              m_nMidiData2;        // Second data byte
    
    // Private MIDI methods
    void ProcessMidiByte(u8 uchData);
    static void MIDIPacketHandler(unsigned nCable, u8 *pPacket, unsigned nLength);
    static void USBDeviceRemovedHandler(CDevice *pDevice, void *pContext);
    
    static CAudio *s_pThis;              // For static callbacks

public:
    CAudio(CInterruptSystem *pInterrupt, CI2CMaster *pI2CMaster);
    ~CAudio(void);
    
    void Process();
    void PutChunk(const u32 *pBuffer, unsigned nChunkSize) override;
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override;
    
    // Accessors for buffer size parameters
    static unsigned GetAudioBlockSize() { return AUDIO_BLOCK_SIZE; }
    static unsigned GetNumFrames() { return NUM_FRAMES; }
    static unsigned GetSampleRate() { return SAMPLE_RATE; }

    // MIDI methods
    #ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    void SetMidiSerial(CSerialDevice *pSerial) { 
            m_pMidiSerial = pSerial; 
    }
    #endif
    void ProcessMidi(boolean bPlugAndPlayUpdated);  // Call this from main loop
};

#endif // _audio_hpp_