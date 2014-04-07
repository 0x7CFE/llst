//include cmath
#include "timer.h"
#include <math.h>
#include <sstream>
#define nanosec_in_sec 1000000000
#define microsec_in_sec 1000000
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
    double diff = current.tv_sec*microsec_in_sec + current.tv_usec/(double)TMicrosec.den
               - (timeCreate.tv_sec*microsec_in_sec + timeCreate.tv_usec/(double)TMicrosec.den);
    return diff;
}
#endif


template <> std::string TDuration<TDay>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "days" : sMode == SuffixMode::SSHORT ? "d" : ""; }

template <> std::string TDuration<THour>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "hours" : sMode == SuffixMode::SSHORT ? "h" : ""; }

template <> std::string TDuration<TMin>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "minutes" : sMode == SuffixMode::SSHORT ? "m" : ""; }

template <> std::string TDuration<TSec>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "seconds" : sMode == SuffixMode::SSHORT ? "s" : ""; }

template <> std::string TDuration<TMillisec>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "milliseconds" : sMode == SuffixMode::SSHORT ? "ms" : ""; }

template <> std::string TDuration<TMicrosec>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "microseconds" : sMode == SuffixMode::SSHORT ? "mcs" : ""; }

template <> std::string TDuration<TNanosec>::getSuffix(SuffixMode sMode){
	return sMode == SuffixMode::SFULL ? "nanoseconds" : sMode == SuffixMode::SSHORT ? "us" : ""; }



