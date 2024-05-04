#ifndef NIRVANA_MODULEMETADATA_H_
#define NIRVANA_MODULEMETADATA_H_
#pragma once

#include <Nirvana/Nirvana.h>
#include <string>
#include <istream>
#include <ostream>

namespace Nirvana {

enum class ModuleType
{
	MODULE_UNKNOWN,
	MODULE_CLASS_LIBRARY,
	MODULE_SINGLETON,
	MODULE_EXECUTABLE,
	MODULE_ERROR
};

struct ModuleMetadataEntry
{
	OLF_Command command;
	unsigned flags;
	std::string name;
	std::string interface_id;
};

struct ModuleMetadata
{
	ModuleType type;
	unsigned platform;
	std::vector <ModuleMetadataEntry> entries;
	std::string error;

	ModuleMetadata () :
		type (ModuleType::MODULE_UNKNOWN),
		platform (0)
	{}

	bool check () noexcept;

	void set_error (std::string msg) noexcept
	{
		type = ModuleType::MODULE_ERROR;
		error = std::move (msg);
	}

	void print (std::ostream& out);
};

extern ModuleMetadata get_module_metadata (std::istream& file);

}

#endif
