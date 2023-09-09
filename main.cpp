#include <Nirvana/Nirvana.h>
#include <Nirvana/OLF_Iterator.h>
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
    for (OLF_Iterator it (olf->get_data (), olf->get_data_size ()); !it.end (); it.next ()) {
      switch (*it.cur ()) {
        case OLF_IMPORT_INTERFACE:
        case OLF_IMPORT_OBJECT: {
          auto p = reinterpret_cast <const ImportInterface*> (it.cur ());
          imports_.push_back ({ get_string (p->name), get_string (p->interface_id) });
        } break;

        case OLF_EXPORT_INTERFACE: {
          auto p = reinterpret_cast <const ExportInterface*> (it.cur ());
          Interface::EPV** itf = (Interface::EPV**)translate_addr (p->itf);
          Interface::EPV* epv = (Interface::EPV*)translate_addr (*itf);
          exports_.push_back ({ get_string (p->name), get_string (epv->interface_id) });
        } break;

        case OLF_EXPORT_OBJECT: {
          auto p = reinterpret_cast <const ExportObject*> (it.cur ());
          exp_objects_.push_back (get_string (p->name));
        } break;

        case OLF_MODULE_STARTUP: {
          if (startup_.interface_id)
            throw std::runtime_error ("Duplicated OLF_MODULE_STARTUP");
          auto p = reinterpret_cast <const Nirvana::ModuleStartup*> (it.cur ());
          startup_.interface_id = get_string (get_EPV (p->startup)->interface_id);
          startup_.flags = p->flags;
        } break;
      }
    }
  }

  void print ()
  {
    if (startup_.interface_id) {
      std::cout << "Startup interface: " << startup_.interface_id << std::endl;
    } else
      std::cout << "No startup interface.\n";
    std::cout << "IMPORTS: " << imports_.size () << std::endl;
    for (const auto& imp : imports_) {
      std::cout << '\t' << imp.name << '\t' << imp.interface_id << std::endl;
    }
    std::cout << "EXPORTS: " << exports_.size () << std::endl;
    for (const auto& imp : exports_) {
      std::cout << '\t' << imp.name << '\t' << imp.interface_id << std::endl;
    }
    std::cout << "OBJECTS: " << exp_objects_.size () << std::endl;
    for (auto p : exp_objects_) {
      std::cout << '\t' << p << std::endl;
    }
  }

private:
  const void* translate_addr (const void* p) const;

  const char* get_string (const char* p)
  {
    return reinterpret_cast <const char*> (translate_addr (p));
  }

  const Interface::EPV* get_EPV (const Interface* itf)
  {
    return reinterpret_cast <const Interface::EPV*> (
      translate_addr (*reinterpret_cast <const void* const*> (translate_addr (itf))));
  }
private:
  coffi reader_;
  uintptr_t image_base_;
  Startup startup_;
  std::vector <ImpEx> imports_;
  std::vector <ImpEx> exports_;
  std::vector <const char*> exp_objects_;
};

const void* ModuleInfo::translate_addr (const void* p) const
{
  uintptr_t va = (uintptr_t)p - image_base_;
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
