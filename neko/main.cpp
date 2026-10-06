#include "save_state_commands.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argumentCount, const char *argumentValues[])
{
  if (argumentCount < 2 ||
      std::string(argumentValues[1]) != "save-state")
  {
    std::cerr <<
      "Usage: neko save-state <inspect|locate|diff> ...\n";
    return 2;
  }

  std::vector<std::string> arguments;
  for (int index = 2; index < argumentCount; ++index)
  {
    arguments.emplace_back(argumentValues[index]);
  }
  return neko_cli::runSaveStateCommand(
    arguments,
    std::cout,
    std::cerr);
}
