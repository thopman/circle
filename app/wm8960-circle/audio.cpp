#include <circle/timer.h>
#include <circle/synchronize.h>
#include <circle/memory.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <assert.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <algorithm>

#include <cstring>
#include <cstdint>
#include <cmath>

#include "circleFaustDSP.h"
#include "audio.hpp"
#include "kernel.h"


static const char FromAudioDevice[] = "audio_device";

// Constants for 24-bit conversion (left-justified format)
static constexpr float kScale24ToFloat = 1.0f / 8388608.0f;  // 2^-23
static constexpr float kScaleFloatTo24 = 8388608.0f;        // 2^23
static constexpr int32_t kMin24 = -(1 << 23);               // -8388608
static constexpr int32_t kMax24 = (1 << 23) - 1;            // +8388607

// Static instance pointer for callbacks
CAudio *CAudio::s_pThis = 0;

CAudio::CAudio(CInterruptSystem *pInterrupt, CI2CMaster *pI2CMaster)
    : CI2SSoundBaseDevice(pInterrupt, SAMPLE_RATE, AUDIO_BLOCK_SIZE, FALSE, pI2CMaster, I2C_ADDRESS, CI2SSoundBaseDevice::DeviceModeTXRX, 2U),
      aCircleFaustDSP(nullptr),
      currentInputBuffer(inputBuffer_A),
      currentOutputBuffer(outputBuffer_A),
      useBufferA(true),
#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
      m_pMidiSerial(nullptr),
      m_bUseSerial(FALSE),
      m_nSerialState(0),
#endif
#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
      m_pMIDIDevice(0),
      m_pKeyboard(0),
#endif
      m_nMidiType(0),
      m_nMidiChannel(0),
      m_nMidiData1(0),
      m_nMidiData2(0)
{   
    s_pThis = this;  // Set static pointer for callbacks

    // Initialize all buffers to zero
    memset(inputBuffer_A, 0, sizeof(inputBuffer_A));
    memset(inputBuffer_B, 0, sizeof(inputBuffer_B));
    memset(outputBuffer_A, 0, sizeof(outputBuffer_A));
    memset(outputBuffer_B, 0, sizeof(outputBuffer_B));
    memset(inputLeft, 0, sizeof(inputLeft));
    memset(inputRight, 0, sizeof(inputRight));
    memset(outputLeft, 0, sizeof(outputLeft));
    memset(outputRight, 0, sizeof(outputRight));
    
    // Create Faust DSP
    aCircleFaustDSP = new circleFaustDSP(SAMPLE_RATE, NUM_FRAMES, 2, 2);
    
    if (aCircleFaustDSP) {
        // Set up DSP channel buffers - Faust will process these directly
        aCircleFaustDSP->setDSP_ChannelBuffers(outputLeft, outputRight, inputLeft, inputRight);
    }

    #ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    // Initialize serial MIDI if device is set later
    memset(m_SerialMessage, 0, sizeof(m_SerialMessage));
    #endif
}

CAudio::~CAudio(void)
{
    s_pThis = 0;

    if (aCircleFaustDSP) {
        delete aCircleFaustDSP;
        aCircleFaustDSP = nullptr;
    }
}

// Convert interleaved I2S input (left-justified 24-bit) to separate float channels
void CAudio::convertAndDeinterleave()
{
    for (unsigned int i = 0; i < NUM_FRAMES; i++) {
        // Left channel: extract from even indices
        // 24-bit sample is in bits 31-8, so arithmetic right shift by 8
        int32_t leftSample = (int32_t)currentInputBuffer[i * 2] >> 8;
        inputLeft[i] = (float)leftSample * kScale24ToFloat;
        
        // Right channel: extract from odd indices
        int32_t rightSample = (int32_t)currentInputBuffer[i * 2 + 1] >> 8;
        inputRight[i] = (float)rightSample * kScale24ToFloat;
    }
}

void CAudio::convertAndInterleave()
{
    for (unsigned int i = 0; i < NUM_FRAMES; i++) {
        // Left channel: clamp and convert using std::max/std::min
        float leftClamped = std::max(-1.0f, std::min(outputLeft[i], 0.999999f));
        int32_t leftSample = (int32_t)(leftClamped * kScaleFloatTo24);
        leftSample = std::max(kMin24, std::min(leftSample, kMax24));
        currentOutputBuffer[i * 2] = (uint32_t)leftSample << 8;  // Left-justify to bits 31-8
        
        // Right channel: clamp and convert using std::max/std::min
        float rightClamped = std::max(-1.0f, std::min(outputRight[i], 0.999999f));
        int32_t rightSample = (int32_t)(rightClamped * kScaleFloatTo24);
        rightSample = std::max(kMin24, std::min(rightSample, kMax24));
        currentOutputBuffer[i * 2 + 1] = (uint32_t)rightSample << 8;  // Left-justify to bits 31-8
    }
}

// PutChunk: Receive I2S input and convert to float
void CAudio::PutChunk(const u32 *pBuffer, unsigned nChunkSize)
{
    if (nChunkSize != AUDIO_BLOCK_SIZE) {
        return; // Silent error handling in audio callback
    }
    
    // Copy I2S input to our buffer
    if (useBufferA) {
        memcpy(inputBuffer_A, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_A;
    } else {
        memcpy(inputBuffer_B, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_B;
    }
    
    // Convert interleaved I2S input to separate float arrays
    convertAndDeinterleave();
}

// GetChunk: Process Faust DSP and send I2S output
unsigned CAudio::GetChunk(u32 *pBuffer, unsigned nChunkSize)
{
    if (nChunkSize != AUDIO_BLOCK_SIZE) {
        return 0; // Silent error handling in audio callback
    }
    
    // Set current output buffer for conversion
    if (useBufferA) {
        currentOutputBuffer = outputBuffer_A;
    } else {
        currentOutputBuffer = outputBuffer_B;
    }
    
    // Process audio through Faust DSP
    if (aCircleFaustDSP) {
        aCircleFaustDSP->processAudioCallback();
    } else {
        // Fallback: copy input to output if no DSP (for testing)
        memcpy(outputLeft, inputLeft, sizeof(inputLeft));
        memcpy(outputRight, inputRight, sizeof(inputRight));
    }
    
    // Convert separate float arrays back to interleaved I2S output
    convertAndInterleave();
    
    // Copy to I2S output buffer
    memcpy(pBuffer, currentOutputBuffer, nChunkSize * sizeof(u32));
    
    // Switch buffers for next iteration
    useBufferA = !useBufferA;
    
    return nChunkSize;
}

void CAudio::Process()
{
    // This method can be used for setting up volume or jack detection if needed
}


void CAudio::ProcessMidi(boolean bPlugAndPlayUpdated)
{
#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
    // Handle USB MIDI device detection
    if (m_pMIDIDevice == 0 && bPlugAndPlayUpdated)
    {
        m_pMIDIDevice = (CUSBMIDIDevice *) CDeviceNameService::Get()->GetDevice("umidi1", FALSE);
        if (m_pMIDIDevice != 0)
        {
            m_pMIDIDevice->RegisterRemovedHandler(USBDeviceRemovedHandler);
            m_pMIDIDevice->RegisterPacketHandler(MIDIPacketHandler);
            
            CLogger::Get()->Write("audio", LogNotice, "USB MIDI device connected");
            return;  // USB MIDI takes priority
        }
    }
    
    // Handle USB keyboard detection (optional, for PC keyboard MIDI input)
    if (m_pKeyboard == 0 && bPlugAndPlayUpdated && m_pMIDIDevice == 0)
    {
        m_pKeyboard = (CUSBKeyboardDevice *) CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);
        if (m_pKeyboard != 0)
        {
            m_pKeyboard->RegisterRemovedHandler(USBDeviceRemovedHandler);
            // Note: keyboard handling would need additional implementation
            CLogger::Get()->Write("audio", LogNotice, "USB keyboard connected");
        }
    }
#endif

#ifdef USE_SERIAL_MIDI || USE_ALL_MIDI_INPUTS
    // Handle Serial MIDI
    if (!m_bUseSerial && m_pMidiSerial)
    {
        m_bUseSerial = TRUE;
        CLogger::Get()->Write("audio", LogNotice, "Serial MIDI enabled");
    }
    
    if (m_bUseSerial && m_pMidiSerial)
    {
        // Read serial MIDI data
        u8 Buffer[32];
        int nResult = m_pMidiSerial->Read(Buffer, sizeof(Buffer));
        
        if (nResult > 0)
        {
            // Process MIDI messages
            for (int i = 0; i < nResult; i++)
            {
                u8 uchData = Buffer[i];
                
                switch (m_nSerialState)
                {
                case 0:
                MIDIRestart:
                    if ((uchData & 0xE0) == 0x80      // Note on/off, all channels
                        || (uchData & 0xF0) == 0xB0)  // MIDI CC, all channels
                    {
                        m_SerialMessage[m_nSerialState++] = uchData;
                    }
                    break;
                    
                case 1:
                case 2:
                    if (uchData & 0x80)  // Got status when parameter expected
                    {
                        m_nSerialState = 0;
                        goto MIDIRestart;
                    }
                    
                    m_SerialMessage[m_nSerialState++] = uchData;
                    
                    if (m_nSerialState == 3)  // Message is complete
                    {
                        MIDIPacketHandler(0, m_SerialMessage, sizeof(m_SerialMessage));
                        m_nSerialState = 0;
                    }
                    break;
                    
                default:
                    assert(0);
                    break;
                }
            }
        }
    }
#endif
}

void CAudio::MIDIPacketHandler(unsigned nCable, u8 *pPacket, unsigned nLength)
{
    assert(s_pThis != 0);
    
    // The packet contents are normal MIDI data
    if (nLength < 3)
    {
        return;
    }
    
    u8 ucStatus    = pPacket[0];
    u8 ucChannel   = ucStatus & 0x0F;
    u8 ucType      = ucStatus >> 4;
    u8 ucKeyNumber = pPacket[1];
    u8 ucVelocity  = pPacket[2];
    
    // Convert MIDI message to FAUST format and send to DSP
    if (s_pThis->aCircleFaustDSP)
    {
        // Convert to FAUST propagateMidi format:
        // propagateMidi(count, time, type, channel, data1, data2)
        double time = 0.0;  // Could use actual timestamp if needed
        
        if (ucType == 0x8 || ucType == 0x9)  // Note on/off
        {
            int midiType = (ucType == 0x9 && ucVelocity > 0) ? 0x90 : 0x80;
            s_pThis->aCircleFaustDSP->propagateMidi(3, time, midiType, ucChannel, ucKeyNumber, ucVelocity);
        }
        else if (ucType == 0xB)  // Control Change
        {
            s_pThis->aCircleFaustDSP->propagateMidi(3, time, 0xB0, ucChannel, ucKeyNumber, ucVelocity);
        }
        else if (ucType == 0xC)  // Program Change
        {
            s_pThis->aCircleFaustDSP->propagateMidi(2, time, 0xC0, ucChannel, ucKeyNumber, 0);
        }
        else if (ucType == 0xD)  // Channel Pressure
        {
            s_pThis->aCircleFaustDSP->propagateMidi(2, time, 0xD0, ucChannel, ucKeyNumber, 0);
        }
        else if (ucType == 0xE)  // Pitch Bend
        {
            s_pThis->aCircleFaustDSP->propagateMidi(3, time, 0xE0, ucChannel, ucKeyNumber, ucVelocity);
        }
    }
}

void CAudio::USBDeviceRemovedHandler(CDevice *pDevice, void *pContext)
{
    assert(s_pThis != 0);
    
#ifdef USE_USB_MIDI_HOST || USE_ALL_MIDI_INPUTS
    if (s_pThis->m_pMIDIDevice == (CUSBMIDIDevice *) pDevice)
    {
        CLogger::Get()->Write("audio", LogNotice, "USB MIDI device removed");
        s_pThis->m_pMIDIDevice = 0;
    }
    else if (s_pThis->m_pKeyboard == (CUSBKeyboardDevice *) pDevice)
    {
        CLogger::Get()->Write("audio", LogNotice, "USB keyboard removed");
        s_pThis->m_pKeyboard = 0;
    }
#endif
}