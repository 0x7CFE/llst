
#include "timer.h"
#include <math.h>
#include <sstream>
using std::stringstream;

#ifdef WINDOWS
void Timer::start(){
    timeCreate = clock();
}

double Timer::getDiffSec(){
    return ((double)(clock() - timeCreate))/CLOCKS_PER_SEC;
}
#else
void Timer::start(){
    gettimeofday(&timeCreate, 0);
}

double Timer::getDiffSec(){
    timeval current;
    gettimeofday(&current, 0);
    double diff = current.tv_sec + current.tv_usec/(double)TMicrosec::den
               - (timeCreate.tv_sec + timeCreate.tv_usec/(double)TMicrosec::den);
    return diff;
}
#endif


template <> std::string TDuration<TDay>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "days" : sMode == SSHORT ? "days" : ""; }

template <> std::string TDuration<THour>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "hours" : sMode == SSHORT ? "hours" : ""; }

template <> std::string TDuration<TMin>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "minutes" : sMode == SSHORT ? "mins" : ""; }

template <> std::string TDuration<TSec>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "seconds" : sMode == SSHORT ? "secs" : ""; }

template <> std::string TDuration<TMillisec>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "milliseconds" : sMode == SSHORT ? "msecs" : ""; }

template <> std::string TDuration<TMicrosec>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "microseconds" : sMode == SSHORT ? "mcsecs" : ""; }

template <> std::string TDuration<TNanosec>::getSuffix(SuffixMode sMode){
    return sMode == SFULL ? "nanoseconds" : sMode == SSHORT ? "usecs" : ""; }



