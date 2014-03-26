#ifdef WINDOWS
#include <time.h>
#endif

struct TTime{
    int sec;
    int msec;
};

class Timer{
private:
#ifdef WINDOWS
    clock_t timeCreate;
#else
    timeval timeCreate;
#endif
    TTime timeDiff; 
public:
    Timer();
    Timer* update();
    int getSec();
    int getMSec();
    TTime getTime();
};