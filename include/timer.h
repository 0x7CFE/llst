#ifdef WINDOWS
#include <time.h>
#else
#include <sys/time.h>
#endif

#ifdef WINDOWS
    typedef clock_t timeValue;
#else
    typedef timeval timeValue;
#endif

struct TTime{
    int sec;
    int msec;
};

class Timer{
private:
    timeValue timeCreate;
    TTime timeDiff; 
public:
    Timer();
    Timer* update();
    int getSec();
    int getMSec();
    TTime getTime();
};