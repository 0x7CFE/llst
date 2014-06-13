
#include <string>
#include <sstream>
#include <math.h> 
using std::stringstream;
#include <time.h>
#include <iomanip>

#ifdef UNIX
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
class TDuration{
private:
    double value;
public:
    //argument: duration in target ratio value
    TDuration(double duration){
        value = duration;
    }
    
    TDuration() {value = 0;}
    
    bool isEmpty() { return value == 0;}
    
    template <typename RATIO2> TDuration<RATIO2> convertTo(){
        return TDuration<RATIO2>(value * (RATIO::num * RATIO2::den) / (double)(RATIO::den * RATIO2::num));
    }
    int toInt(){
        return floor(value);
    }
    std::string toString(SuffixMode sMode = SNONE, int symbolsAfterPoint = 0,
                                      const char* pointSymbol = ".", const char* spaceSymbol = " "){
        stringstream ss; 
        ss << floor(value);
        if(symbolsAfterPoint)
            ss << pointSymbol << std::setfill('0') << std::setw(symbolsAfterPoint) 
               << floor((value - floor(value)) * pow(10.0, symbolsAfterPoint));
        if(sMode != SNONE)
            ss << spaceSymbol << getSuffix(sMode);
        return ss.str();
    }
    std::string getSuffix(SuffixMode sMode);
    
    std::ostream& operator<<(std::ostream& os){
        os << toString();
        return os;
    }

    inline bool operator< (const TDuration<RATIO>& rhs){
        return value < rhs.value;
    }

    inline TDuration<RATIO> operator+(const TDuration<RATIO>& rhs){
        return TDuration<RATIO>(value + rhs.value);
    }

    inline TDuration<RATIO> operator-(const TDuration<RATIO>& rhs){
        return TDuration<RATIO>(value - rhs.value);
    }

    inline bool operator> (const TDuration<RATIO>& rhs){return  !(operator< (rhs));}

    friend class Timer;
};


class Timer{
private:
    systemTimeValue timeCreate; 
    double getDiffSec();
public:
    //timer which count time from specified unix-time.
    Timer(time_t time);
    //default constructor is implicit Timer::now()
    Timer(){ this->start();}
    static Timer now() {Timer t; t.start(); return t;}
    void start();
    template <typename RATIO>
    TDuration<RATIO> get(){
        return TDuration<TSec>(getDiffSec()).convertTo<RATIO>();
    }
};