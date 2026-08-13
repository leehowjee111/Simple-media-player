#include "header/player.h"
#include <iostream>
int main(int argc, char *argv[]) {
  Player player(10240, 10240);
  if (!player.init(argc, argv[1])) {
    std::cerr << "failed\n";
    return 1;
  }
  player.start();
  return 0;
}
