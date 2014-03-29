#ifdef WINDOWS
#include <time.h>
#else
#include <sys/time.h>
#endif

#ifdef WINDOWS
    typedef clock_t systemTimeValue;
#else
    typedef timeval systemTimeValue;
#endif

    
//analogue of c++11 ratio from chrono.h
template <int NUM, int DEN>
struct TRatio
{
    const int num = NUM;
    const int den = DEN;
};

typedef TRatio<86400,1>      TDay;
typedef TRatio<3600,1>       THour;
typedef TRatio<60,1>         TMin;
typedef TRatio<1,1>          TSec;
typedef TRatio<1,1000>       TMillisec;
typedef TRatio<1,1000000>    TMicrosec;
typedef TRatio<1,1000000000> TNanosec;

//fixed point format 
//represent nanoseconds
typedef unsigned long long TTime;

//analogue of c++11 duration
template <class RATIO>
class TDuraton{
private:
    TTime time;
public:
    TDuraton(TTime duration);
    template <class RATIO2> extern RATIO2 convertTo();
    char* toString();
    TTime getValue();
};

template <class RATIO> extern
std::ostream& operator<<(std::ostream& os, const TDuraton<RATIO>& obj);

template <class RATIO, class RATIO2> extern
inline bool operator< (const TDuraton<RATIO>& lhs, const TDuraton<RATIO2>& rhs);
template <class RATIO, class RATIO2> extern
inline bool operator> (const TDuraton<RATIO>& lhs, const TDuraton<RATIO2>& rhs){return  operator< (rhs,lhs);}

template <class RATIO, class RATIO2> extern
inline TDuraton<RATIO> operator+(TDuraton<RATIO> lhs, const TDuraton<RATIO2>& rhs);
template <class RATIO, class RATIO2> extern
inline TDuraton<RATIO> operator-(TDuraton<RATIO> lhs, const TDuraton<RATIO2>& rhs);



class Timer{
private:
    systemTimeValue timeCreate;
    TTime timeDiff; 
public:
    Timer();
    static Timer now();
    void start();
    Timer* update();
    TTime getTime();
};