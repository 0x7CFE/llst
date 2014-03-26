#include "timer.h"
#include <cmath>

Timer::Timer(){
#ifdef WINDOWS
    clock_t timeCreate = clock();
#else
    gettimeofday(&timeCreate, 0);
#endif
}

Timer* Timer::update(){
#ifdef WINDOWS
    float diff = ((float)(clock() - timeCreate))/CLOCKS_PER_SEC;
#else
   timeval current;
   gettimeofday(&current, 0);
   float diff = current.tv_sec + ((float)(current.tv_usec/1000))/1000.f 
              - (timeCreate.tv_sec + ((float)(timeCreate.tv_usec/1000))/1000.f);
#endif
   timeDiff.sec = (int)trunc(diff);
   timeDiff.msec = (int)(diff - timeDiff.sec);
   return this;
}

TTime Timer::getTime(){
    return timeDiff;
}