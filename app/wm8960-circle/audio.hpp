#ifndef _audio_hpp_
#define _audio_hpp_

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
};

#endif // _audio_hpp_