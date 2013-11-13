#include <memory.h>
#include <types.h>
#include <map>

namespace ib {

struct ImageClass {
    std::string name;
    std::string parent;
    std::vector<std::string> instanceVariables;
    std::map<std::string, ImageMethod> methods;
};

struct ImageMethod {
    std::string className;
    std::string name;
    std::vector<std::string> temporaries;
    std::vector<std::string> arguments;
    std::vector<uint8_t> bytecodes;
};

class ImageBuilder {
private:
    std::map<std::string, ImageClass> m_imageObjects;

public:
};

class MethodCompiler {
private:
    ImageMethod m_currentMethod;

public:
    bool compile(const std::string& className, const std::string& methodSource);
};

}