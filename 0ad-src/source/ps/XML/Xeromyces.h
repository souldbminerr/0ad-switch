/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
  Xeromyces file-loading interface.
  Automatically creates and caches relatively
  efficient binary representations of XML files.
*/

#ifndef INCLUDED_XEROMYCES
#define INCLUDED_XEROMYCES

#include "ps/Errors.h"
#include "ps/Singleton.h"
#include "ps/XMB/XMBData.h"
#include "ps/XMB/XMBStorage.h"

#include <string>

class RelaxNGValidator;

ERROR_GROUP(Xeromyces);
ERROR_TYPE(Xeromyces, XMLOpenFailed);
ERROR_TYPE(Xeromyces, XMLParseError);
ERROR_TYPE(Xeromyces, XMLValidationFailed);

class CXeromyces : public XMBData
{
	friend class TestXMBData;
	friend class XMBData;
public:
	/**
	 * Load from an XML file (with invisible XMB caching).
	 */
	PSRETURN Load(const PIVFS& vfs, const VfsPath& filename, const std::string& validatorName = "");

	/**
	 * Load from an in-memory XML string (with no caching).
	 */
	PSRETURN LoadString(const char* xml, const std::string& validatorName = "");

	/**
	 * Convert the given XML file into an XMB in the archive cache.
	 * Returns the XMB path in @p archiveCachePath.
	 * Returns false on error.
	 */
	bool GenerateCachedXMB(const PIVFS& vfs, const VfsPath& sourcePath, VfsPath& archiveCachePath, const std::string& validatorName = "");

private:
	PSRETURN ConvertFile(const PIVFS& vfs, const VfsPath& filename, const VfsPath& xmbPath, const std::string& validatorName);

	XMBStorage m_Data;
};

class CXeromycesEngine : public Singleton<CXeromycesEngine>
{
	friend CXeromyces;
public:
	/**
	 * This should be run in the main thread, before any thread uses libxml2.
	 */
	CXeromycesEngine();
	~CXeromycesEngine();

	bool AddValidator(const PIVFS& vfs, const std::string& name, const VfsPath& grammarPath);
	bool ValidateEncoded(const std::string& name, const std::string& filename, const std::string& document);

private:
	RelaxNGValidator& GetValidator(const std::string& name);
};

#define g_Xeromyces CXeromycesEngine::GetSingleton()

#define _XERO_MAKE_UID2__(p,l) p ## l
#define _XERO_MAKE_UID1__(p,l) _XERO_MAKE_UID2__(p,l)

#define _XERO_CHILDREN _XERO_MAKE_UID1__(_children_, __LINE__)
#define _XERO_I _XERO_MAKE_UID1__(_i_, __LINE__)

#define XERO_ITER_EL(parent_element, child_element)					\
	for (XMBElement child_element : parent_element.GetChildNodes())

#define XERO_ITER_ATTR(parent_element, attribute)						\
	for (XMBAttribute attribute : parent_element.GetAttributes())

#endif // INCLUDED_XEROMYCES
