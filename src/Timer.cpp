#include "timer.h"
#include <cmath>

#ifdef WINDOWS
Timer::Timer(){
    clock_t timeCreate = clock();
}

Timer* Timer::update(){
    float diff = ((float)(clock() - timeCreate))/CLOCKS_PER_SEC;
    timeDiff.sec = (int)trunc(diff);
    timeDiff.msec = (int)(diff - timeDiff.sec);
    return this;
}
#else
Timer::Timer(){
    gettimeofday(&timeCreate, 0);
}

Timer* Timer::update(){
    timeval current;
    gettimeofday(&current, 0);
    float diff = current.tv_sec + ((float)(current.tv_usec/1000))/1000.f 
               - (timeCreate.tv_sec + ((float)(timeCreate.tv_usec/1000))/1000.f);
    timeDiff.sec = (int)trunc(diff);
    timeDiff.msec = (int)(diff - timeDiff.sec);
    return this;
}
#endif

TTime Timer::getTime(){
    return timeDiff;
}