#include "bios_startup_command.hpp"
#include "save_state_commands.hpp"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

int main(int argumentCount, const char *argumentValues[])
{
  if (argumentCount < 2)
  {
    std::cerr <<
      "Usage: neko save-state <inspect|locate|diff|iop-memory|ee-memory|frame> ...\n"
      "       neko bios-startup <file> --cycles <count> "
      "[--trace-events <count> [--failure-context] [--trace-from-cycle <count>]] "
      "[--resume-checkpoint <file>] [--save-checkpoint <file>]\n";
    return 2;
  }

  std::vector<std::string> arguments;
  for (int index = 2; index < argumentCount; ++index)
  {
    arguments.emplace_back(argumentValues[index]);
  }
  const std::string command = argumentValues[1];
  if (command == "save-state")
  {
#ifdef _WIN32
    if (!arguments.empty() && arguments[0] == "frame" &&
        _setmode(_fileno(stdout), _O_BINARY) == -1)
    {
      std::cerr << "Cannot configure binary frame output.\n";
      return 2;
    }
#endif
    return neko_cli::runSaveStateCommand(arguments, std::cout, std::cerr);
  }
  if (command == "bios-startup")
  {
    return neko_cli::runBIOSStartupCommand(arguments, std::cout, std::cerr);
  }
  std::cerr << "Unknown command: " << command << '\n';
  return 2;
}
