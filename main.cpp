#include <Nirvana/Nirvana.h>
#include <Nirvana/OLF_Iterator.h>
#include <Nirvana/platform.h>
#include <coffi/coffi.hpp>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <stdexcept>

using namespace COFFI;
using namespace Nirvana;
using namespace CORBA;
using namespace CORBA::Internal;

class ModuleInfo
{
  struct ImpEx
  {
    const char* name;
    const char* interface_id;

    bool operator < (const ImpEx& rhs) const
    {
      return std::strcmp (name, rhs.name) < 0;
    }
  };

  struct Startup
  {
    const char* interface_id;
    uintptr_t flags;

    Startup () :
      interface_id (nullptr),
      flags (0)
    {}
  };

public:
  ModuleInfo (const char* file_name) :
    image_base_ (0)
  {
    std::ifstream file;
    file.open (file_name, std::ios::in | std::ios::binary);
    if (!file)
      throw std::runtime_error ("File not found");
    if (!reader_.load (file))
      throw std::runtime_error ("Can't process COFF file");
    machine_ = reader_.get_header ()->get_machine ();
    bits_ = 32;
    switch (machine_) {
    case PLATFORM_X64:
    case PLATFORM_ARM64:
      bits_ = 64;
      break;
    }

    const auto wh = reader_.get_win_header ();
    image_base_ = (uintptr_t)wh->get_image_base ();
    const sections& svec = reader_.get_sections ();
    const section* olf = nullptr;
    for (const section* psec : svec) {
      if (psec->get_name () == OLF_BIND) {
        olf = psec;
        break;
      }
    }
    if (!olf)
      throw std::runtime_error ("Not a Nirvana module");

    if (bits_ == 32)
      iterate <uint32_t> (*olf);
    else
      iterate <uint64_t> (*olf);
  }

  template <typename Word>
  void iterate (const section& olf)
  {
    for (OLF_Iterator <Word> it (olf.get_data (), olf.get_data_size ()); !it.end (); it.next ()) {
      switch (*it.cur ()) {
      case OLF_IMPORT_INTERFACE: {
        auto p = reinterpret_cast <const ImportInterfaceW <Word>*> (it.cur ());
        imports_.push_back ({ get_string (p->name), get_string (p->interface_id) });
      } break;

      case OLF_IMPORT_OBJECT: {
        auto p = reinterpret_cast <const ImportInterfaceW <Word>*> (it.cur ());
        imp_objects_.push_back ({ get_string (p->name), get_string (p->interface_id) });
      } break;

      case OLF_EXPORT_INTERFACE: {
        auto p = reinterpret_cast <const ExportInterfaceW <Word>*> (it.cur ());
        exports_.push_back ({ get_string (p->name), get_string (get_EPV <Word> (p->itf)->interface_id) });
      } break;

      case OLF_EXPORT_OBJECT: {
        auto p = reinterpret_cast <const ExportObjectW <Word>*> (it.cur ());
        exp_objects_.push_back (get_string (p->name));
      } break;

      case OLF_MODULE_STARTUP: {
        if (startup_.interface_id)
          throw std::runtime_error ("Duplicated OLF_MODULE_STARTUP");
        auto p = reinterpret_cast <const Nirvana::ModuleStartupW <Word>*> (it.cur ());
        startup_.interface_id = get_string (get_EPV <Word> (p->startup)->interface_id);
        startup_.flags = p->flags;
      } break;
      }
    }
  }

  void print ()
  {
    const char* platform = "Unknown platform";
    switch (machine_) {
    case PLATFORM_I386:
      platform = "Intel 386";
      break;
    case PLATFORM_X64:
      platform = "AMD64";
      break;
    case PLATFORM_ARM:
      platform = "ARM Little-Endian";
      break;
    case PLATFORM_ARM64:
      platform = "ARM64 Little-Endian";
      break;
    }

    std::cout << "Platform: " << platform << std::endl;

    if (startup_.interface_id)
      std::cout << "Startup interface: " << startup_.interface_id << std::endl;
    else
      std::cout << "No startup interface.\n";

    sort (imports_);
    sort (exports_);
    sort (imp_objects_);
    sort (exp_objects_);

    std::cout << "IMPORT INTERFACES: " << imports_.size () << std::endl;
    for (const auto& imp : imports_) {
      std::cout << '\t' << imp.name << '\t' << imp.interface_id << std::endl;
    }
    std::cout << "EXPORT INTERFACES: " << exports_.size () << std::endl;
    for (const auto& imp : exports_) {
      std::cout << '\t' << imp.name << '\t' << imp.interface_id << std::endl;
    }
    std::cout << "IMPORT OBJECTS: " << imp_objects_.size () << std::endl;
    for (const auto& imp : imp_objects_) {
      std::cout << '\t' << imp.name << '\t' << imp.interface_id << std::endl;
    }
    std::cout << "EXPORT OBJECTS: " << exp_objects_.size () << std::endl;
    for (auto p : exp_objects_) {
      std::cout << '\t' << p << std::endl;
    }
  }

private:
  const void* translate_addr (uintptr_t p) const;

  const char* get_string (uintptr_t p)
  {
    return reinterpret_cast <const char*> (translate_addr (p));
  }

  template <typename Word>
  struct InterfaceEPV {
    Word interface_id;
  };

  template <typename Word>
  const InterfaceEPV <Word>* get_EPV (uintptr_t itf)
  {
    return reinterpret_cast <const InterfaceEPV <Word>*> (
      translate_addr (*reinterpret_cast <const Word*> (translate_addr (itf))));
  }

  static void sort (std::vector <ImpEx>& v);
  static void sort (std::vector <const char*>& v);

private:
  coffi reader_;
  uintptr_t image_base_;
  uint16_t machine_;
  uint16_t bits_;
  Startup startup_;
  std::vector <ImpEx> imports_;
  std::vector <ImpEx> exports_;
  std::vector <ImpEx> imp_objects_;
  std::vector <const char*> exp_objects_;
};

const void* ModuleInfo::translate_addr (uintptr_t p) const
{
  uintptr_t va = p - image_base_;
  const section* section = nullptr;
  for (const auto psec : reader_.get_sections ()) {
    uint32_t begin = psec->get_virtual_address ();
    if (begin <= va) {
      uint32_t end = begin + psec->get_virtual_size ();
      if (end > va) {
        section = psec;
        break;
      }
    }
  }
  if (!section)
    throw std::logic_error ("Can not translate address");
  return section->get_data () + (va - section->get_virtual_address ());
}

void ModuleInfo::sort (std::vector <ImpEx>& v)
{
  std::sort (v.begin (), v.end ());
}

void ModuleInfo::sort (std::vector <const char*>& v)
{
  std::sort (v.begin (), v.end (), [](const char* a, const char* b) {return strcmp (a, b) < 0; });
}

int main (int argc, char* argv [])
{
  if (argc != 2) {
    std::cout << "dumpolf <module file name>\n";
    return 0;
  }
  try {
    ModuleInfo info (argv [1]);
    info.print ();
  } catch (const std::exception& ex) {
    std::cerr << ex.what () << std::endl;
    return -1;
  }
  return 0;
}
