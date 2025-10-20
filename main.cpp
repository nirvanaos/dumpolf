#include <Nirvana/ModuleMetadata.h>
#include <iostream>
#include <fstream>

int main (int argc, char* argv [])
{
  if (argc != 2) {
    std::cout << "dumpolf <module file name>\n";
    return 0;
  }
  try {
    Nirvana::ModuleMetadata module_metadata;
    {
      std::ifstream file (argv [1], std::ios::in | std::ios::binary);
      if (!file)
        throw std::runtime_error ("File not found");
      const char* ext = strrchr (argv [1], '.');
      bool exe = ext && !strcmp (ext, ".exe");
      module_metadata = Nirvana::get_module_metadata (file, exe);
    }
    module_metadata.print (std::cout);
  } catch (const std::exception& ex) {
    std::cerr << ex.what () << std::endl;
    return -1;
  }
  return 0;
}
