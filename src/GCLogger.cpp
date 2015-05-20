#include "memory.h"


GCLogger::GCLogger(const char* fileName):
	m_logFile(fileName, std::fstream::out)
{}

GCLogger::~GCLogger(){
    m_logFile.flush();
}

enum MeasuringConstants { bytes_in_kb = 1024 };


void GCLogger::writeLogLine(TMemoryManagerEvent event){
    m_logFile << event.begin.toString(SNONE, 3)
              << ": [" << event.eventName << " ";
    if(!event.heapInfo.empty()){
        TMemoryManagerHeapInfo eh = event.heapInfo;
        m_logFile << eh.usedHeapSizeBeforeCollect / bytes_in_kb << "K->"
                  << eh.usedHeapSizeAfterCollect / bytes_in_kb << "K("
                  << eh.totalHeapSize / bytes_in_kb << "K)";
        for(std::list<TMemoryManagerHeapEvent>::iterator i = eh.heapEvents.begin(); i != eh.heapEvents.end(); i++){
            m_logFile << "[" << i->eventName << ": "
                      << i->usedHeapSizeBeforeCollect / bytes_in_kb << "K->"
                      << i->usedHeapSizeAfterCollect / bytes_in_kb << "K("
                      << i->totalHeapSize / bytes_in_kb << "K)";
            if(!i->timeDiff.isEmpty())
                m_logFile << ", " << i->timeDiff.toString(SSHORT, 6);
            m_logFile << "] ";
        }
    }
    if(!event.timeDiff.isEmpty())
        m_logFile << ", " << event.timeDiff.toString(SSHORT, 6);
    //gc-viewer see error when no delay or delay is 0.0
    else 
	m_logFile << ", 0.000001 secs";
    m_logFile << "]\n";
}
