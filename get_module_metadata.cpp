#include "ModuleMetadata.h"
#include <Nirvana/OLF_Iterator.h>
#include <Nirvana/platform.h>
#include <coffi/coffi.hpp>
#include <iostream>
#include <stdexcept>

using namespace Nirvana;

class ModuleReader : public COFFI::coffi
{
public:
	Nirvana::ModuleMetadata get_module_metadata () const
	{
		Nirvana::ModuleMetadata md;
		md.platform = get_header ()->get_machine ();
		unsigned bits = 32;
		switch (md.platform) {
		case PLATFORM_X64:
		case PLATFORM_ARM64:
			bits = 64;
			break;
		}
		const COFFI::section* olf = nullptr;
		for (const COFFI::section* psec : get_sections ()) {
			if (psec->get_name () == OLF_BIND) {
				olf = psec;
				break;
			}
		}

		if (!olf)
			md.set_error ("Metadata not found");
		else {
			bool valid;
			if (bits == 32)
				valid = iterate <uint32_t> (*olf, md);
			else
				valid = iterate <uint64_t> (*olf, md);

			if (!valid)
				md.set_error ("Invalid metadata");
		}

		return md;
	}

private:
	template <typename Word>
	bool iterate (const COFFI::section& olf, ModuleMetadata& md) const
	{
		for (OLF_Iterator <Word> it (olf.get_data (), olf.get_data_size ()); !it.end (); it.next ()) {
			if (!it.valid ())
				return false;

			OLF_Command command = (OLF_Command)*it.cur ();
			switch (command) {
			case OLF_IMPORT_INTERFACE:
			case OLF_IMPORT_OBJECT: {
				auto p = reinterpret_cast <const ImportInterfaceW <Word>*> (it.cur ());
				md.entries.push_back ({ command, 0, get_string (p->name), get_string (p->interface_id) });
			} break;

			case OLF_EXPORT_INTERFACE: {
				auto p = reinterpret_cast <const ExportInterfaceW <Word>*> (it.cur ());
				md.entries.push_back ({ command, 0, get_string (p->name), get_string (get_EPV <Word> (p->itf)->interface_id) });
			} break;

			case OLF_EXPORT_OBJECT:
			case OLF_EXPORT_LOCAL: {
				auto p = reinterpret_cast <const ExportObjectW <Word>*> (it.cur ());
				md.entries.push_back ({ command, 0, get_string (p->name), get_string (get_EPV <Word> (p->servant)->interface_id) });
			} break;

			case OLF_MODULE_STARTUP: {
				auto p = reinterpret_cast <const Nirvana::ModuleStartupW <Word>*> (it.cur ());
				md.entries.push_back ({ command, (unsigned)(p->flags), std::string (), get_string (get_EPV <Word> (p->startup)->interface_id) });
			} break;
			}
		}

		return true;
	}

	const void* translate_addr (uint64_t p) const;

	const char* get_string (uint64_t p) const
	{
		return reinterpret_cast <const char*> (translate_addr (p));
	}

	template <typename Word>
	struct InterfaceEPV {
		Word interface_id;
	};

	template <typename Word>
	const InterfaceEPV <Word>* get_EPV (uint64_t itf) const
	{
		return reinterpret_cast <const InterfaceEPV <Word>*> (
			translate_addr (*reinterpret_cast <const Word*> (translate_addr (itf))));
	}

};

const void* ModuleReader::translate_addr (uint64_t p) const
{
	uint32_t va = (uint32_t)(p - get_win_header ()->get_image_base ());
	const COFFI::section* section = nullptr;
	for (const auto psec : get_sections ()) {
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

namespace Nirvana {

ModuleMetadata get_module_metadata (std::istream& file)
{
	ModuleReader reader;
	if (!reader.load (file)) {
		ModuleMetadata md;
		md.set_error ("Can't read COFF file");
		return md;
	}

	return reader.get_module_metadata ();
}

}
