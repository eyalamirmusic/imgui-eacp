#include "Common.h"

// The rendered cases construct views and drive them through renderToImage, and
// a view needs the platform's app object to exist before it can be built at
// all. So the suite runs inside eacp::Apps::run, exactly as eacp's own GPU
// suite does.

namespace
{
int argCount = 0;
char** argValues = nullptr;
int exitCode = 0;

void runTests()
{
    exitCode = nano::run(argCount, argValues);
}
} // namespace

int main(int argc, char* argv[])
{
    argCount = argc;
    argValues = argv;

    eacp::Apps::run(runTests);
    return exitCode;
}
