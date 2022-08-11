#include <Nirvana/basic_string.h>
#include <Nirvana/vector.h>
#include <Nirvana/OLF_Iterator.h>
#include <Nirvana/OLF.h>
#include <coffi/coffi.hpp>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <stdexcept>

using namespace COFFI;
using namespace Nirvana;
using namespace CORBA;
using namespace CORBA::Internal;
using namespace std;

class ModuleInfo
{
  struct Import
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
    ifstream file;
    file.open (file_name, std::ios::in | std::ios::binary);
    if (!file)
      throw runtime_error ("File not found");
    if (!reader_.load (file))
      throw runtime_error ("Can't process COFF file");
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
      throw runtime_error ("Not a Nirvana module");
    for (OLF_Iterator it (olf->get_data (), olf->get_data_size ()); !it.end (); it.next ()) {
      switch (*it.cur ()) {
        case OLF_IMPORT_INTERFACE: {
          auto p = reinterpret_cast <const ImportInterface*> (it.cur ());
          imports_.push_back ({ get_string (p->name), get_string (p->interface_id) });
        } break;

        case OLF_MODULE_STARTUP: {
          if (startup_.interface_id)
            throw runtime_error ("Duplicated OLF_MODULE_STARTUP");
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
      cout << "Startup interface: " << startup_.interface_id << endl;
    } else
      cout << "No startup interface.\n";
    cout << "IMPORTS: " << imports_.size () << endl;
    for (const auto& imp : imports_) {
      cout << '\t' << imp.name << '\t' << imp.interface_id << endl;
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
  vector <Import> imports_;
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
    throw logic_error ("Can not translate address");
  return section->get_data () + (va - section->get_virtual_address ());
}

int main (int argc, char* argv [])
{
  if (argc != 2) {
    cout << "dumpolf <module file name>\n";
    return 0;
  }
  try {
    ModuleInfo info (argv [1]);
    info.print ();
  } catch (const exception& ex) {
    cerr << ex.what () << endl;
    return -1;
  }
  return 0;
}
