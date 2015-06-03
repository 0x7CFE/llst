
#ifndef LLST_TIMER_H_INCLUDED
#define LLST_TIMER_H_INCLUDED

#include <string>
#include <sstream>
#include <math.h>
using std::stringstream;
#include <time.h>
#include <iomanip>

#if defined(unix) || defined(__unix__) || defined(__unix)
    #include <sys/time.h>
    typedef timeval systemTimeValue;
#else
    typedef clock_t systemTimeValue;
#endif


//analogue of c++11 ratio from chrono.h
template <int NUM, int DEN>
struct TRatio
{
    static const int num = NUM;
    static const int den = DEN;
};

typedef TRatio<86400,1>      TDay;
typedef TRatio<3600,1>       THour;
typedef TRatio<60,1>         TMin;
typedef TRatio<1,1>          TSec;
typedef TRatio<1,1000>       TMillisec;
typedef TRatio<1,1000000>    TMicrosec;
typedef TRatio<1,1000000000> TNanosec;


//type used to show string representation of ratio
enum SuffixMode {SNONE, SSHORT, SFULL};

//analogue of c++11 duration
template <typename RATIO>
class TDuration
{
private:
    double value;
public:
    TDuration() : value(0) { }
    TDuration(double duration) : value(duration) { }

    bool isEmpty() const { return value == 0;}

    template <typename RATIO2> TDuration<RATIO2> convertTo() const {
        return TDuration<RATIO2>(value * (RATIO::num * RATIO2::den) / (double)(RATIO::den * RATIO2::num));
    }
    int toInt() {
        return floor(value);
    }
    double toDouble() {
        return value;
    }
    std::string toString(
        SuffixMode sMode = SNONE,
        int symbolsAfterPoint = 0,
        const char* pointSymbol = ".",
        const char* spaceSymbol = " "
    ) const {
        stringstream ss;
        ss << floor(value);
        if (symbolsAfterPoint)
            ss << pointSymbol << std::setfill('0') << std::setw(symbolsAfterPoint)
               << floor((value - floor(value)) * pow(10.0, symbolsAfterPoint));
        if (sMode != SNONE)
            ss << spaceSymbol << getSuffix(sMode);
        return ss.str();
    }
    std::string getSuffix(SuffixMode sMode) const;

    std::ostream& operator<<(std::ostream& os) const {
        os << this->toString();
        return os;
    }

    bool operator< (const TDuration& rhs) const {
        return value < rhs.value;
    }

    TDuration operator+(const TDuration& rhs) const {
        return TDuration(value + rhs.value);
    }

    TDuration operator-(const TDuration& rhs) const {
        return TDuration(value - rhs.value);
    }

    bool operator> (const TDuration& rhs) const {
        return !(operator< (rhs)); // FIXME: it works as >=
    }

    friend class Timer;
};


class Timer {
private:
    systemTimeValue timeCreate;
    double getDiffSec() const;
public:
    //timer which count time from specified unix-time.
    Timer(time_t time);
    //default constructor is implicit Timer::now()
    Timer(){ this->start(); }
    static Timer now() { return Timer(); }
    void start();
    template <typename RATIO>
    TDuration<RATIO> get() const {
        return TDuration<TSec>(getDiffSec()).convertTo<RATIO>();
    }
};

#endif
