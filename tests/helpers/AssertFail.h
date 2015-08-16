#ifndef LLST_ASSERT_FAIL_INCLUDED
#define LLST_ASSERT_FAIL_INCLUDED

void __assert_fail(const char *__assertion, const char *__file, unsigned int __line, const char */*__function*/)
{
    std::stringstream ss;
    ss << "Assertion '" << __assertion << "' failed in '" << __file << "' at: " << __line;
    throw ss.str();
}

#endif
