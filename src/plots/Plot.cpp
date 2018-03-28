// ==========================================================================
// 
// creepMiner - Burstcoin cryptocurrency CPU and GPU miner
// Copyright (C)  2016-2018 Creepsky (creepsky@gmail.com)
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110 - 1301  USA
// 
// ==========================================================================

#include "Plot.hpp"
#include <Poco/SHA1Engine.h>
#include <Poco/DigestStream.h>
#include "mining/Miner.hpp"
#include <Poco/File.h>
#include <Poco/DirectoryIterator.h>
#include "logging/Message.hpp"
#include "logging/MinerLogger.hpp"
#include "MinerUtil.hpp"

Burst::PlotFile::PlotFile(const std::string& path, const Poco::UInt64 size)
	: PlotFile{std::string{path}, size}
{}

Burst::PlotFile::PlotFile(std::string&& path, const Poco::UInt64 size)
	: path_(move(path)), size_(size)
{
	accountId_ = stoull(getAccountIdFromPlotFile(path_));
	nonceStart_ = stoull(getStartNonceFromPlotFile(path_));
	nonces_ = stoull(getNonceCountFromPlotFile(path_));
	staggerSize_ = stoull(getStaggerSizeFromPlotFile(path_));
}

const std::string& Burst::PlotFile::getPath() const
{
	return path_;
}

Poco::UInt64 Burst::PlotFile::getSize() const
{
	return size_;
}

Poco::UInt64 Burst::PlotFile::getAccountId() const
{
	return accountId_;
}

Poco::UInt64 Burst::PlotFile::getNonceStart() const
{
	return nonceStart_;
}

Poco::UInt64 Burst::PlotFile::getNonces() const
{
	return nonces_;
}

Poco::UInt64 Burst::PlotFile::getStaggerSize() const
{
	return staggerSize_;
}

Poco::UInt64 Burst::PlotFile::getStaggerCount() const
{
	return getNonces() / getStaggerSize();
}

Poco::UInt64 Burst::PlotFile::getStaggerBytes() const
{
	return getStaggerSize() * Settings::PlotSize;
}

Poco::UInt64 Burst::PlotFile::getStaggerScoopBytes() const
{
	return getStaggerSize() * Settings::ScoopSize;
}

std::string Burst::PlotFile::toString() const
{
	return Poco::format("%Lu_%Lu_%Lu_%Lu", getAccountId(), getNonceStart(), getNonces(), getStaggerSize());
}

Burst::PlotDir::PlotDir(std::string path, const Type type)
	: path_{std::move(path)},
	  type_{type},
	  size_{0}
{
	addPlotLocation(path_);
	recalculateHash();
}

Burst::PlotDir::PlotDir(std::string path, const std::vector<std::string>& relatedPaths, Type type)
	: path_{std::move(path)},
	  type_{type},
	  size_{0}
{
	addPlotLocation(path_);

	for (const auto& relatedPath : relatedPaths)
		relatedDirs_.emplace_back(new PlotDir{relatedPath, type_});

	recalculateHash();
}

Burst::PlotDir::PlotList Burst::PlotDir::getPlotfiles(bool recursive) const
{
	// copy all plot files inside this plot directory
	PlotList plotFiles;

	plotFiles.insert(plotFiles.end(), plotfiles_.begin(), plotfiles_.end());

	if (recursive)
	{
		// copy also all plot files inside all related plot directories
		for (const auto& relatedPlotDir : getRelatedDirs())
		{
			auto relatedPlotFiles = relatedPlotDir->getPlotfiles(true);
			plotFiles.insert(std::end(plotFiles), std::begin(relatedPlotFiles), std::end(relatedPlotFiles));
		}
	}

	return plotFiles;
}

const std::string& Burst::PlotDir::getPath() const
{
	return path_;
}

Poco::UInt64 Burst::PlotDir::getSize() const
{
	return size_;
}

Burst::PlotDir::Type Burst::PlotDir::getType() const
{
	return type_;
}

std::vector<std::shared_ptr<Burst::PlotDir>> Burst::PlotDir::getRelatedDirs() const
{
	return relatedDirs_;
}

const std::string& Burst::PlotDir::getHash() const
{
	return hash_;
}

void Burst::PlotDir::rescan()
{
	plotfiles_.clear();
	size_ = 0;

	addPlotLocation(path_);

	for (auto& relatedDir : relatedDirs_)
		relatedDir->rescan();

	recalculateHash();
}

bool Burst::PlotDir::addPlotLocation(const std::string& fileOrPath)
{
	try
	{
		Poco::Path path;

		if (!path.tryParse(fileOrPath))
		{
			log_warning(MinerLogger::config, "%s is an invalid file/dir (syntax), skipping it!", fileOrPath);
			return false;
		}

		Poco::File fileOrDir{path};

		if (!fileOrDir.exists())
		{
			log_warning(MinerLogger::config, "Plot file/dir does not exist: '%s'", path.toString());
			return false;
		}

		// its a single plot file, add it if its really a plot file
		if (fileOrDir.isFile())
			return addPlotFile(fileOrPath) != nullptr;

		// its a dir, so we need to parse all plot files in it and add them
		if (fileOrDir.isDirectory())
		{
			Poco::DirectoryIterator iter{fileOrDir};
			const Poco::DirectoryIterator end;

			while (iter != end)
			{
				if (iter->isFile())
					addPlotFile(*iter);

				++iter;
			}

			return true;
		}

		return false;
	}
	catch (...)
	{
		return false;
	}
}

std::shared_ptr<Burst::PlotFile> Burst::PlotDir::addPlotFile(const Poco::File& file)
{
	const auto result = isValidPlotFile(file.path());

	if (result == PlotCheckResult::Ok)
	{
		// plot file is already in our list
		for (auto& plotfile : plotfiles_)
			if (plotfile->getPath() == file.path())
				return plotfile;

		// make a new plotfile and add it to the list
		auto plotFile = std::make_shared<PlotFile>(std::string(file.path()), file.getSize());
		plotfiles_.emplace_back(plotFile);
		size_ += file.getSize();

		return plotFile;
	}

	if (result == PlotCheckResult::EmptyParameter)
		return nullptr;

	std::string errorString;

	if (result == PlotCheckResult::Incomplete)
		errorString = "The plotfile is incomplete!";

	if (result == PlotCheckResult::InvalidParameter)
		errorString = "The plotfile has invalid parameters!";

	if (result == PlotCheckResult::WrongStaggersize)
		errorString = "The plotfile has an invalid staggersize!";

	log_warning(MinerLogger::config, "Found an invalid plotfile, skipping it!\n\tPath: %s\n\tReason: %s", file.path(), errorString);
	return nullptr;
}

void Burst::PlotDir::recalculateHash()
{
	Poco::SHA1Engine sha;
	Poco::DigestOutputStream shaStream{sha};

	hash_.clear();

	for (const auto& plotFile : getPlotfiles(true))
		shaStream << plotFile->getPath();

	shaStream << std::flush;
	hash_ = Poco::SHA1Engine::digestToHex(sha.digest());
}

Poco::UInt64 Burst::PlotHelper::checkPlotOverlaps(const std::vector<std::shared_ptr<PlotFile>>& plotFiles)
{
	Poco::UInt64 totalOverlaps = 0;

	for (const auto& lhs : plotFiles)
	{
		for (const auto& rhs : plotFiles)
		{
			if (lhs == rhs || lhs->getAccountId() != rhs->getAccountId())
				// same plotfile or different account, skip
				continue;
			
			Poco::UInt64 checkStartNonceFirst = 0,
			             checkNoncesFirst = 0,
			             checkStartNonceSecond = 0,
			             checkNoncesSecond = 0;

			const std::string* pathFirst = nullptr;
			const std::string* pathSecond = nullptr;

			// start nonce of rhs is inside of nonce range of lhs
			if (rhs->getNonceStart() >= lhs->getNonceStart() &&
				rhs->getNonceStart() < lhs->getNonceStart() + lhs->getNonces())
			{
				checkStartNonceFirst = lhs->getNonceStart();
				checkNoncesFirst = lhs->getNonces();
				checkStartNonceSecond = rhs->getNonceStart();
				checkNoncesSecond = rhs->getNonces();
				pathFirst = &lhs->getPath();
				pathSecond = &rhs->getPath();
			}
			// start nonce of lhs is inside of nonce range of rhs
			else if (lhs->getNonceStart() >= rhs->getNonceStart() &&
				lhs->getNonceStart() < rhs->getNonceStart() + rhs->getNonces())
			{
				checkStartNonceFirst = rhs->getNonceStart();
				checkNoncesFirst = rhs->getNonces();
				checkStartNonceSecond = lhs->getNonceStart();
				checkNoncesSecond = lhs->getNonces();
				pathFirst = &rhs->getPath();
				pathSecond = &lhs->getPath();
			}

			if (pathFirst != nullptr && pathSecond != nullptr)
			{
				auto overlap = checkStartNonceFirst + checkNoncesFirst - checkStartNonceSecond;

				if (checkNoncesSecond < overlap)
					overlap = checkNoncesSecond;

				log_error(MinerLogger::miner, "%s and %s overlap by %Lu nonces", *pathFirst, *pathSecond, overlap);
				totalOverlaps++;
			}
		}
	}

	return totalOverlaps;
}
