
#include <string>
#include <sstream>
using std::stringstream;
#ifdef WINDOWS
	#include <time.h>
    typedef clock_t systemTimeValue;
#else
	#include <sys/time.h>
    typedef timeval systemTimeValue;
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


//!тип вне TDuration потому что получается много типов 
//TDuration<RATIO>::SuffixMode что не логично
enum SuffixMode {SNONE, SSHORT, SFULL};

//analogue of c++11 duration
template <typename RATIO>
class TDuration{
private:
	double value;
public:
	//argument: duration in seconds
	//FIXME should be a private member, but make conflict at convertTo() in such case
	TDuration(double duration){
		value = duration;
	}

	template <typename RATIO2> TDuration<RATIO2> convertTo(){
		return TDuration<RATIO2>(value * (RATIO::num * RATIO2::den) / (double)(RATIO::den * RATIO2::num));
	}

	std::string toString(SuffixMode sMode = SuffixMode::SNONE, int symbolsAfterPoint = 0,
									  const char* pointSymbol = ".", const char* spaceSymbol = " "){
		stringstream ss; 
		ss << floor(value);
		if(symbolsAfterPoint)
			ss << pointSymbol << floor((value - floor(value)) * pow(10.0, symbolsAfterPoint));
		if(sMode != SuffixMode::SNONE)
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
	Timer(){}
	static Timer now() {Timer t; t.start(); return t;}
    void start();
	template <typename RATIO>
	TDuration<RATIO> get(){
		return TDuration<TSec>(getDiffSec()).convertTo<RATIO>();
	}
};