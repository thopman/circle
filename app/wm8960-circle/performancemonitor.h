//
// performancemonitor.h
//
// Performance monitoring for Faust DSP processing
// Uses Circle framework timer for high-resolution timing
//

#ifndef _performancemonitor_h
#define _performancemonitor_h

#include <circle/timer.h>
#include <circle/types.h>
#include <circle/logger.h>

class CPerformanceMonitor
{
public:
    CPerformanceMonitor(const char* name = "DSP", unsigned maxSamples = 1000);
    ~CPerformanceMonitor();
    
    // Basic timing interface
    void StartTiming();
    void EndTiming();
    void EndTiming(unsigned numSamplesProcessed);
    
    // Statistics access
    float GetAverageCPUUsagePercent() const;
    float GetMaxCPUUsagePercent() const;
    unsigned GetAverageClockCycles() const;
    unsigned GetMaxClockCycles() const;
    float GetAverageProcessingTimeUs() const;
    float GetMaxProcessingTimeUs() const;
    
    // Reporting
    void LogStatistics() const;
    void ResetStatistics();
    
    // Configuration
    void SetSampleRate(unsigned sampleRate) { m_nSampleRate = sampleRate; }
    void SetBufferSize(unsigned bufferSize) { m_nBufferSize = bufferSize; }
    
    // Real-time safe version for use in audio callback
    struct TimingResult {
        unsigned clockCycles;
        float processingTimeUs;
        float cpuUsagePercent;
    };
    TimingResult GetLastTimingResult() const { return m_LastResult; }

private:
    const char* m_pName;
    unsigned m_nSampleRate;
    unsigned m_nBufferSize;
    unsigned m_nMaxSamples;
    
    // Timing state
    unsigned m_nStartTicks;
    unsigned m_nEndTicks;
    bool m_bTimingActive;
    
    // Statistics storage
    unsigned* m_pClockCycles;
    float* m_pProcessingTimes;
    float* m_pCPUUsages;
    unsigned m_nCurrentIndex;
    unsigned m_nValidSamples;
    
    // Cached results
    TimingResult m_LastResult;
    
    // Private helper methods
    float CalculateAverage(const float* data, unsigned count) const;
    float CalculateMax(const float* data, unsigned count) const;
    unsigned CalculateAverage(const unsigned* data, unsigned count) const;
    unsigned CalculateMax(const unsigned* data, unsigned count) const;
    float ClockTicksToMicroseconds(unsigned ticks) const;
    float CalculateCPUUsage(float processingTimeUs, unsigned numSamples) const;
    
    static const char FromPerformanceMonitor[];
};

// RAII timing helper for automatic start/stop
class CTimingScope
{
public:
    CTimingScope(CPerformanceMonitor& monitor) : m_Monitor(monitor)
    {
        m_Monitor.StartTiming();
    }
    
    ~CTimingScope()
    {
        m_Monitor.EndTiming();
    }
    
private:
    CPerformanceMonitor& m_Monitor;
};

// Macro for easy RAII timing
#define PERFORMANCE_TIME_SCOPE(monitor) CTimingScope _scope(monitor)

#endif // _performancemonitor_h